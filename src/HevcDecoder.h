#pragma once

#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <vector>

struct AVCodecContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

extern std::atomic<long> g_objectCount;

class HevcDecoder final : public IMFTransform {
public:
  HevcDecoder();
  ~HevcDecoder();

  HevcDecoder(const HevcDecoder&) = delete;
  HevcDecoder& operator=(const HevcDecoder&) = delete;

  // IUnknown
  STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;

  // IMFTransform
  STDMETHODIMP GetStreamLimits(DWORD* inputMinimum, DWORD* inputMaximum,
                               DWORD* outputMinimum, DWORD* outputMaximum) override;
  STDMETHODIMP GetStreamCount(DWORD* inputStreams, DWORD* outputStreams) override;
  STDMETHODIMP GetStreamIDs(DWORD inputIDArraySize, DWORD* inputIDs,
                            DWORD outputIDArraySize, DWORD* outputIDs) override;
  STDMETHODIMP GetInputStreamInfo(DWORD streamID, MFT_INPUT_STREAM_INFO* streamInfo) override;
  STDMETHODIMP GetOutputStreamInfo(DWORD streamID, MFT_OUTPUT_STREAM_INFO* streamInfo) override;
  STDMETHODIMP GetAttributes(IMFAttributes** attributes) override;
  STDMETHODIMP GetInputStreamAttributes(DWORD streamID, IMFAttributes** attributes) override;
  STDMETHODIMP GetOutputStreamAttributes(DWORD streamID, IMFAttributes** attributes) override;
  STDMETHODIMP DeleteInputStream(DWORD streamID) override;
  STDMETHODIMP AddInputStreams(DWORD streams, DWORD* streamIDs) override;
  STDMETHODIMP GetInputAvailableType(DWORD streamID, DWORD typeIndex,
                                     IMFMediaType** type) override;
  STDMETHODIMP GetOutputAvailableType(DWORD streamID, DWORD typeIndex,
                                      IMFMediaType** type) override;
  STDMETHODIMP SetInputType(DWORD streamID, IMFMediaType* type, DWORD flags) override;
  STDMETHODIMP SetOutputType(DWORD streamID, IMFMediaType* type, DWORD flags) override;
  STDMETHODIMP GetInputCurrentType(DWORD streamID, IMFMediaType** type) override;
  STDMETHODIMP GetOutputCurrentType(DWORD streamID, IMFMediaType** type) override;
  STDMETHODIMP GetInputStatus(DWORD streamID, DWORD* flags) override;
  STDMETHODIMP GetOutputStatus(DWORD* flags) override;
  STDMETHODIMP SetOutputBounds(LONGLONG lowerBound, LONGLONG upperBound) override;
  STDMETHODIMP ProcessEvent(DWORD streamID, IMFMediaEvent* event) override;
  STDMETHODIMP ProcessMessage(MFT_MESSAGE_TYPE message, ULONG_PTR parameter) override;
  STDMETHODIMP ProcessInput(DWORD streamID, IMFSample* sample, DWORD flags) override;
  STDMETHODIMP ProcessOutput(DWORD flags, DWORD outputBufferCount,
                             MFT_OUTPUT_DATA_BUFFER* outputSamples,
                             DWORD* status) override;

private:
  HRESULT CreateMediaType(GUID subtype, IMFMediaType** type) const;
  HRESULT CopySampleBytes(IMFSample* sample, std::vector<uint8_t>& bytes) const;
  HRESULT CreateOrUseOutputSample(MFT_OUTPUT_DATA_BUFFER* output, IMFSample** sample) const;
  HRESULT CopyDecodedFrame(const AVFrame* frame, IMFSample* sample);
  HRESULT ConfigureDecoder(IMFMediaType* type);
  void ReleaseDecoder();
  void ClearInputState();

  std::atomic<ULONG> m_refCount{1};
  Microsoft::WRL::ComPtr<IMFAttributes> m_attributes;
  Microsoft::WRL::ComPtr<IMFMediaType> m_inputType;
  Microsoft::WRL::ComPtr<IMFMediaType> m_outputType;

  AVCodecContext* m_codecContext = nullptr;
  AVPacket* m_packet = nullptr;
  AVFrame* m_frame = nullptr;
  SwsContext* m_scaleContext = nullptr;

  GUID m_outputSubtype = GUID_NULL;
  UINT32 m_width = 0;
  UINT32 m_height = 0;
  LONGLONG m_sampleTime = 0;
  LONGLONG m_sampleDuration = 0;
  bool m_packetSubmitted = false;
  bool m_draining = false;
};

class HevcClassFactory final : public IClassFactory {
public:
  HevcClassFactory();
  ~HevcClassFactory();

  STDMETHODIMP QueryInterface(REFIID riid, void** object) override;
  STDMETHODIMP_(ULONG) AddRef() override;
  STDMETHODIMP_(ULONG) Release() override;
  STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override;
  STDMETHODIMP LockServer(BOOL lock) override;

private:
  std::atomic<ULONG> m_refCount{1};
};
