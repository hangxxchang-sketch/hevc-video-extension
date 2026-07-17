#include "HevcDecoder.h"
#include "Guids.h"

#include <mferror.h>
#include <mfapi.h>
#include <mftransform.h>

#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#include <algorithm>
#include <cstring>
#include <new>

std::atomic<long> g_objectCount{0};

namespace {

bool IsHevcSubtype(REFGUID subtype) {
  return subtype == MFVideoFormat_HEVC || subtype == MFVideoFormat_HEVC_ES;
}

bool IsSupportedOutputSubtype(REFGUID subtype) {
  return subtype == MFVideoFormat_NV12 || subtype == MFVideoFormat_P010;
}

HRESULT AvError(int error) {
  if (error >= 0) return S_OK;
  if (error == AVERROR(EAGAIN)) return MF_E_TRANSFORM_NEED_MORE_INPUT;
  if (error == AVERROR_EOF) return MF_E_TRANSFORM_NEED_MORE_INPUT;
  return E_FAIL;
}

UINT32 OutputBufferSize(UINT32 width, UINT32 height, REFGUID subtype) {
  if (subtype == MFVideoFormat_P010) {
    return width * height * 3;
  }
  return width * height * 3 / 2;
}

} // namespace

HevcDecoder::HevcDecoder() {
  ++g_objectCount;
  MFCreateAttributes(&m_attributes, 8);
  if (m_attributes) {
    m_attributes->SetUINT32(MF_SA_MINIMUM_OUTPUT_SAMPLE_COUNT, 1);
    m_attributes->SetUINT32(MF_TRANSFORM_ASYNC, FALSE);
    m_attributes->SetUINT32(MFT_SUPPORT_DYNAMIC_FORMAT_CHANGE, FALSE);
  }
  m_packet = av_packet_alloc();
  m_frame = av_frame_alloc();
}

HevcDecoder::~HevcDecoder() {
  ReleaseDecoder();
  av_packet_free(&m_packet);
  av_frame_free(&m_frame);
  --g_objectCount;
}

STDMETHODIMP HevcDecoder::QueryInterface(REFIID riid, void** object) {
  if (!object) return E_POINTER;
  *object = nullptr;
  if (riid == IID_IUnknown || riid == IID_IMFTransform) {
    *object = static_cast<IMFTransform*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) HevcDecoder::AddRef() {
  return ++m_refCount;
}

STDMETHODIMP_(ULONG) HevcDecoder::Release() {
  const ULONG count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

STDMETHODIMP HevcDecoder::GetStreamLimits(DWORD* inputMinimum, DWORD* inputMaximum,
                                           DWORD* outputMinimum, DWORD* outputMaximum) {
  if (!inputMinimum || !inputMaximum || !outputMinimum || !outputMaximum) return E_POINTER;
  *inputMinimum = *inputMaximum = 1;
  *outputMinimum = *outputMaximum = 1;
  return S_OK;
}

STDMETHODIMP HevcDecoder::GetStreamCount(DWORD* inputStreams, DWORD* outputStreams) {
  if (!inputStreams || !outputStreams) return E_POINTER;
  *inputStreams = 1;
  *outputStreams = 1;
  return S_OK;
}

STDMETHODIMP HevcDecoder::GetStreamIDs(DWORD, DWORD*, DWORD, DWORD*) {
  return E_NOTIMPL;
}

STDMETHODIMP HevcDecoder::GetInputStreamInfo(DWORD streamID, MFT_INPUT_STREAM_INFO* streamInfo) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!streamInfo) return E_POINTER;
  *streamInfo = {};
  streamInfo->dwFlags = MFT_INPUT_STREAM_WHOLE_SAMPLES |
                        MFT_INPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER;
  streamInfo->hnsMaxLatency = 0;
  streamInfo->cbSize = 0;
  streamInfo->cbMaxLookahead = 0;
  streamInfo->cbAlignment = 1;
  return S_OK;
}

STDMETHODIMP HevcDecoder::GetOutputStreamInfo(DWORD streamID, MFT_OUTPUT_STREAM_INFO* streamInfo) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!streamInfo) return E_POINTER;
  *streamInfo = {};
  streamInfo->dwFlags = MFT_OUTPUT_STREAM_WHOLE_SAMPLES |
                        MFT_OUTPUT_STREAM_SINGLE_SAMPLE_PER_BUFFER |
                        MFT_OUTPUT_STREAM_FIXED_SAMPLE_SIZE;
  streamInfo->cbSize = OutputBufferSize(m_width, m_height, m_outputSubtype);
  streamInfo->cbAlignment = 1;
  return S_OK;
}

STDMETHODIMP HevcDecoder::GetAttributes(IMFAttributes** attributes) {
  if (!attributes) return E_POINTER;
  *attributes = nullptr;
  if (!m_attributes) return E_UNEXPECTED;
  return m_attributes.CopyTo(attributes);
}

STDMETHODIMP HevcDecoder::GetInputStreamAttributes(DWORD streamID, IMFAttributes** attributes) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!attributes) return E_POINTER;
  *attributes = nullptr;
  return MFCreateAttributes(attributes, 4);
}

STDMETHODIMP HevcDecoder::GetOutputStreamAttributes(DWORD streamID, IMFAttributes** attributes) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!attributes) return E_POINTER;
  *attributes = nullptr;
  return MFCreateAttributes(attributes, 4);
}

STDMETHODIMP HevcDecoder::DeleteInputStream(DWORD) {
  return E_NOTIMPL;
}

STDMETHODIMP HevcDecoder::AddInputStreams(DWORD, DWORD*) {
  return E_NOTIMPL;
}

HRESULT HevcDecoder::CreateMediaType(GUID subtype, IMFMediaType** type) const {
  if (!type) return E_POINTER;
  *type = nullptr;
  IMFMediaType* mediaType = nullptr;
  HRESULT hr = MFCreateMediaType(&mediaType);
  if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = mediaType->SetGUID(MF_MT_SUBTYPE, subtype);
  if (SUCCEEDED(hr)) hr = mediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  if (SUCCEEDED(hr)) *type = mediaType;
  else if (mediaType) mediaType->Release();
  return hr;
}

STDMETHODIMP HevcDecoder::GetInputAvailableType(DWORD streamID, DWORD typeIndex,
                                                 IMFMediaType** type) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!type) return E_POINTER;
  if (typeIndex > 1) return MF_E_NO_MORE_TYPES;
  return CreateMediaType(typeIndex == 0 ? MFVideoFormat_HEVC : MFVideoFormat_HEVC_ES, type);
}

STDMETHODIMP HevcDecoder::GetOutputAvailableType(DWORD streamID, DWORD typeIndex,
                                                  IMFMediaType** type) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!type) return E_POINTER;
  if (typeIndex > 1) return MF_E_NO_MORE_TYPES;
  if (!m_inputType) return MF_E_TRANSFORM_TYPE_NOT_SET;

  HRESULT hr = CreateMediaType(typeIndex == 0 ? MFVideoFormat_NV12 : MFVideoFormat_P010, type);
  if (SUCCEEDED(hr) && m_width != 0 && m_height != 0) {
    hr = MFSetAttributeSize(*type, MF_MT_FRAME_SIZE, m_width, m_height);
  }
  return hr;
}

HRESULT HevcDecoder::ConfigureDecoder(IMFMediaType* type) {
  GUID subtype{};
  HRESULT hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
  if (FAILED(hr) || !IsHevcSubtype(subtype)) return MF_E_INVALIDMEDIATYPE;

  UINT32 width = 0;
  UINT32 height = 0;
  if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) || width == 0 || height == 0) {
    return MF_E_INVALIDMEDIATYPE;
  }

  ReleaseDecoder();
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
  if (!codec) return MF_E_NOT_AVAILABLE;

  m_codecContext = avcodec_alloc_context3(codec);
  if (!m_codecContext) return E_OUTOFMEMORY;
  m_codecContext->width = static_cast<int>(width);
  m_codecContext->height = static_cast<int>(height);
  m_codecContext->thread_count = 0;
  m_codecContext->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
  if (avcodec_open2(m_codecContext, codec, nullptr) < 0) {
    ReleaseDecoder();
    return MF_E_NOT_AVAILABLE;
  }

  m_width = width;
  m_height = height;
  return S_OK;
}

STDMETHODIMP HevcDecoder::SetInputType(DWORD streamID, IMFMediaType* type, DWORD flags) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!type) {
    if (flags & MFT_SET_TYPE_TEST_ONLY) return S_OK;
    ReleaseDecoder();
    m_inputType.Reset();
    m_outputType.Reset();
    m_outputSubtype = GUID_NULL;
    m_width = 0;
    m_height = 0;
    ClearInputState();
    return S_OK;
  }

  GUID major{};
  GUID subtype{};
  if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) ||
      FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
      major != MFMediaType_Video || !IsHevcSubtype(subtype)) {
    return MF_E_INVALIDMEDIATYPE;
  }
  if (flags & MFT_SET_TYPE_TEST_ONLY) return S_OK;

  HRESULT hr = ConfigureDecoder(type);
  if (FAILED(hr)) return hr;
  m_inputType = type;
  m_outputType.Reset();
  m_outputSubtype = GUID_NULL;
  ClearInputState();
  return S_OK;
}

STDMETHODIMP HevcDecoder::SetOutputType(DWORD streamID, IMFMediaType* type, DWORD flags) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!type) {
    if (flags & MFT_SET_TYPE_TEST_ONLY) return S_OK;
    m_outputType.Reset();
    m_outputSubtype = GUID_NULL;
    return S_OK;
  }
  if (!m_inputType) return MF_E_TRANSFORM_TYPE_NOT_SET;

  GUID major{};
  GUID subtype{};
  if (FAILED(type->GetGUID(MF_MT_MAJOR_TYPE, &major)) ||
      FAILED(type->GetGUID(MF_MT_SUBTYPE, &subtype)) ||
      major != MFMediaType_Video || !IsSupportedOutputSubtype(subtype)) {
    return MF_E_INVALIDMEDIATYPE;
  }

  UINT32 width = 0;
  UINT32 height = 0;
  if (FAILED(MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height)) ||
      width != m_width || height != m_height) {
    return MF_E_INVALIDMEDIATYPE;
  }
  if (flags & MFT_SET_TYPE_TEST_ONLY) return S_OK;

  m_outputType = type;
  m_outputSubtype = subtype;
  return S_OK;
}

STDMETHODIMP HevcDecoder::GetInputCurrentType(DWORD streamID, IMFMediaType** type) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!type) return E_POINTER;
  *type = nullptr;
  if (!m_inputType) return MF_E_TRANSFORM_TYPE_NOT_SET;
  return m_inputType.CopyTo(type);
}

STDMETHODIMP HevcDecoder::GetOutputCurrentType(DWORD streamID, IMFMediaType** type) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!type) return E_POINTER;
  *type = nullptr;
  if (!m_outputType) return MF_E_TRANSFORM_TYPE_NOT_SET;
  return m_outputType.CopyTo(type);
}

STDMETHODIMP HevcDecoder::GetInputStatus(DWORD streamID, DWORD* flags) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!flags) return E_POINTER;
  *flags = m_packetSubmitted ? 0 : MFT_INPUT_STATUS_ACCEPT_DATA;
  return S_OK;
}

STDMETHODIMP HevcDecoder::GetOutputStatus(DWORD* flags) {
  if (!flags) return E_POINTER;
  *flags = m_packetSubmitted ? MFT_OUTPUT_STATUS_SAMPLE_READY : 0;
  return S_OK;
}

STDMETHODIMP HevcDecoder::SetOutputBounds(LONGLONG, LONGLONG) {
  return E_NOTIMPL;
}

STDMETHODIMP HevcDecoder::ProcessEvent(DWORD, IMFMediaEvent*) {
  return E_NOTIMPL;
}

void HevcDecoder::ClearInputState() {
  m_packetSubmitted = false;
  m_draining = false;
  m_sampleTime = 0;
  m_sampleDuration = 0;
  if (m_packet) av_packet_unref(m_packet);
  if (m_frame) av_frame_unref(m_frame);
}

STDMETHODIMP HevcDecoder::ProcessMessage(MFT_MESSAGE_TYPE message, ULONG_PTR) {
  switch (message) {
    case MFT_MESSAGE_COMMAND_FLUSH:
      if (m_codecContext) avcodec_flush_buffers(m_codecContext);
      ClearInputState();
      return S_OK;
    case MFT_MESSAGE_COMMAND_DRAIN:
      if (!m_codecContext || m_packetSubmitted) return S_OK;
      if (avcodec_send_packet(m_codecContext, nullptr) >= 0) {
        m_packetSubmitted = true;
        m_draining = true;
      }
      return S_OK;
    case MFT_MESSAGE_NOTIFY_END_STREAMING:
      ClearInputState();
      return S_OK;
    default:
      return S_OK;
  }
}

HRESULT HevcDecoder::CopySampleBytes(IMFSample* sample, std::vector<uint8_t>& bytes) const {
  if (!sample) return E_POINTER;
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
  if (FAILED(hr)) return hr;

  BYTE* data = nullptr;
  DWORD maxLength = 0;
  DWORD currentLength = 0;
  hr = buffer->Lock(&data, &maxLength, &currentLength);
  if (SUCCEEDED(hr)) {
    bytes.assign(data, data + currentLength);
    buffer->Unlock();
  }
  return hr;
}

STDMETHODIMP HevcDecoder::ProcessInput(DWORD streamID, IMFSample* sample, DWORD flags) {
  if (streamID != 0) return MF_E_INVALIDSTREAMNUMBER;
  if (!sample) return E_POINTER;
  if (flags != 0) return E_INVALIDARG;
  if (!m_inputType || !m_outputType) return MF_E_TRANSFORM_TYPE_NOT_SET;
  if (m_packetSubmitted) return MF_E_NOTACCEPTING;
  if (!m_packet || !m_codecContext) return E_UNEXPECTED;

  std::vector<uint8_t> bytes;
  HRESULT hr = CopySampleBytes(sample, bytes);
  if (FAILED(hr)) return hr;
  if (bytes.empty()) return MF_E_INVALIDMEDIATYPE;

  av_packet_unref(m_packet);
  if (av_new_packet(m_packet, static_cast<int>(bytes.size())) < 0) return E_OUTOFMEMORY;
  std::memcpy(m_packet->data, bytes.data(), bytes.size());
  LONGLONG sampleTime = 0;
  if (SUCCEEDED(sample->GetSampleTime(&sampleTime))) m_sampleTime = sampleTime;
  LONGLONG sampleDuration = 0;
  if (SUCCEEDED(sample->GetSampleDuration(&sampleDuration))) m_sampleDuration = sampleDuration;

  const int result = avcodec_send_packet(m_codecContext, m_packet);
  if (result < 0 && result != AVERROR(EAGAIN)) {
    av_packet_unref(m_packet);
    return AvError(result);
  }
  m_packetSubmitted = true;
  return S_OK;
}

HRESULT HevcDecoder::CreateOrUseOutputSample(MFT_OUTPUT_DATA_BUFFER* output, IMFSample** sample) const {
  if (!output || !sample) return E_POINTER;
  *sample = output->pSample;
  if (*sample) {
    (*sample)->AddRef();
    return S_OK;
  }

  Microsoft::WRL::ComPtr<IMFSample> newSample;
  HRESULT hr = MFCreateSample(&newSample);
  if (SUCCEEDED(hr)) {
    Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
    hr = MFCreateMemoryBuffer(OutputBufferSize(m_width, m_height, m_outputSubtype), &buffer);
    if (SUCCEEDED(hr)) hr = newSample->AddBuffer(buffer.Get());
  }
  if (SUCCEEDED(hr)) hr = newSample.CopyTo(sample);
  return hr;
}

HRESULT HevcDecoder::CopyDecodedFrame(const AVFrame* frame, IMFSample* sample) {
  if (!frame || !sample) return E_POINTER;
  Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
  HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
  if (FAILED(hr)) return hr;

  BYTE* destination = nullptr;
  DWORD maxLength = 0;
  DWORD currentLength = 0;
  hr = buffer->Lock(&destination, &maxLength, &currentLength);
  if (FAILED(hr)) return hr;

  const AVPixelFormat target = m_outputSubtype == MFVideoFormat_P010
    ? AV_PIX_FMT_P010LE
    : AV_PIX_FMT_NV12;
  const int required = static_cast<int>(OutputBufferSize(m_width, m_height, m_outputSubtype));
  if (maxLength < static_cast<DWORD>(required)) {
    buffer->Unlock();
    return MF_E_BUFFERTOOSMALL;
  }

  uint8_t* destinationPlanes[4] = {destination, nullptr, nullptr, nullptr};
  int destinationStride[4] = {};
  if (target == AV_PIX_FMT_P010LE) {
    destinationPlanes[1] = destination + (m_width * m_height * 2);
    destinationStride[0] = static_cast<int>(m_width * 2);
    destinationStride[1] = static_cast<int>(m_width * 2);
  } else {
    destinationPlanes[1] = destination + (m_width * m_height);
    destinationStride[0] = static_cast<int>(m_width);
    destinationStride[1] = static_cast<int>(m_width);
  }

  m_scaleContext = sws_getCachedContext(
    m_scaleContext,
    frame->width,
    frame->height,
    static_cast<AVPixelFormat>(frame->format),
    static_cast<int>(m_width),
    static_cast<int>(m_height),
    target,
    SWS_BILINEAR,
    nullptr,
    nullptr,
    nullptr);
  if (!m_scaleContext) {
    buffer->Unlock();
    return E_FAIL;
  }

  const int scaled = sws_scale(m_scaleContext,
                               frame->data,
                               frame->linesize,
                               0,
                               frame->height,
                               destinationPlanes,
                               destinationStride);
  buffer->Unlock();
  if (scaled <= 0) return E_FAIL;
  return buffer->SetCurrentLength(required);
}

STDMETHODIMP HevcDecoder::ProcessOutput(DWORD flags, DWORD outputBufferCount,
                                         MFT_OUTPUT_DATA_BUFFER* outputSamples,
                                         DWORD* status) {
  if (flags != 0) return E_INVALIDARG;
  if (!outputSamples || !status) return E_POINTER;
  if (outputBufferCount < 1) return E_INVALIDARG;
  if (!m_inputType || !m_outputType) return MF_E_TRANSFORM_TYPE_NOT_SET;
  *status = 0;
  outputSamples[0].dwStatus = 0;
  outputSamples[0].pEvents = nullptr;

  if (!m_packetSubmitted) return MF_E_TRANSFORM_NEED_MORE_INPUT;
  const int result = avcodec_receive_frame(m_codecContext, m_frame);
  if (result < 0) {
    if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
      m_packetSubmitted = false;
      if (m_draining) m_draining = false;
      return MF_E_TRANSFORM_NEED_MORE_INPUT;
    }
    m_packetSubmitted = false;
    return AvError(result);
  }

  Microsoft::WRL::ComPtr<IMFSample> sample;
  HRESULT hr = CreateOrUseOutputSample(&outputSamples[0], &sample);
  if (SUCCEEDED(hr)) hr = CopyDecodedFrame(m_frame, sample.Get());
  if (SUCCEEDED(hr)) {
    sample->SetSampleTime(m_sampleTime);
    if (m_sampleDuration > 0) sample->SetSampleDuration(m_sampleDuration);
    sample->SetUINT32(MFSampleExtension_CleanPoint, TRUE);
    if (!outputSamples[0].pSample) outputSamples[0].pSample = sample.Detach();
  }
  av_frame_unref(m_frame);
  if (FAILED(hr)) return hr;
  return S_OK;
}

void HevcDecoder::ReleaseDecoder() {
  if (m_scaleContext) sws_freeContext(m_scaleContext);
  m_scaleContext = nullptr;
  avcodec_free_context(&m_codecContext);
}
