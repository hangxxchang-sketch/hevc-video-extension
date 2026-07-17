#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mftransform.h>

#include <iostream>

int wmain() {
  HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) {
    std::wcerr << L"CoInitializeEx failed: 0x" << std::hex << comHr << L"\n";
    return 1;
  }

  HRESULT hr = MFStartup(MF_VERSION);
  if (FAILED(hr)) {
    std::wcerr << L"MFStartup failed: 0x" << std::hex << hr << L"\n";
    if (SUCCEEDED(comHr)) CoUninitialize();
    return 1;
  }

  IMFMediaType* input = nullptr;
  hr = MFCreateMediaType(&input);
  if (SUCCEEDED(hr)) hr = input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
  if (SUCCEEDED(hr)) hr = input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_HEVC);

  IMFActivate** activates = nullptr;
  UINT32 count = 0;
  if (SUCCEEDED(hr)) {
    hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
                   MFT_ENUM_FLAG_ALL,
                   input,
                   nullptr,
                   &activates,
                   &count);
  }

  if (SUCCEEDED(hr)) {
    std::wcout << L"HEVC video decoders found: " << count << L"\n";
    for (UINT32 i = 0; i < count; ++i) {
      WCHAR name[256]{};
      UINT32 length = 0;
      if (SUCCEEDED(activates[i]->GetString(MFT_FRIENDLY_NAME_Attribute, name,
                                            ARRAYSIZE(name), &length))) {
        std::wcout << L"  - " << name << L"\n";
      }
      activates[i]->Release();
    }
    CoTaskMemFree(activates);
  } else {
    std::wcerr << L"MFTEnumEx failed: 0x" << std::hex << hr << L"\n";
  }

  if (input) input->Release();
  MFShutdown();
  if (SUCCEEDED(comHr)) CoUninitialize();
  return SUCCEEDED(hr) ? 0 : 1;
}
