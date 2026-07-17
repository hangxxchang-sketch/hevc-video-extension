#include "HevcDecoder.h"
#include "Guids.h"

#include <mfapi.h>
#include <mferror.h>
#include <strsafe.h>

#include <windows.h>

#include <new>
#include <string>

namespace {

HMODULE g_module = nullptr;
std::atomic<long> g_lockCount{0};

const wchar_t* const kFriendlyName = L"HEVC Video Extension (FFmpeg)";

HRESULT WriteStringValue(HKEY key, const wchar_t* name, const std::wstring& value) {
  return RegSetValueExW(key, name, 0, REG_SZ,
                        reinterpret_cast<const BYTE*>(value.c_str()),
                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
}

HRESULT RegisterComClass() {
  wchar_t modulePath[MAX_PATH]{};
  if (GetModuleFileNameW(g_module, modulePath, ARRAYSIZE(modulePath)) == 0) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  wchar_t clsidString[64]{};
  if (StringFromGUID2(CLSID_HevcVideoExtension, clsidString, ARRAYSIZE(clsidString)) == 0) {
    return E_FAIL;
  }

  const std::wstring root = L"Software\\Classes\\CLSID\\" + std::wstring(clsidString);
  HKEY classKey = nullptr;
  LONG result = RegCreateKeyExW(HKEY_LOCAL_MACHINE, root.c_str(), 0, nullptr,
                                REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &classKey, nullptr);
  if (result != ERROR_SUCCESS) {
    return HRESULT_FROM_WIN32(result);
  }

  HRESULT hr = WriteStringValue(classKey, nullptr, kFriendlyName);
  HKEY serverKey = nullptr;
  if (SUCCEEDED(hr)) {
    result = RegCreateKeyExW(classKey, L"InprocServer32", 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &serverKey, nullptr);
    hr = HRESULT_FROM_WIN32(result);
  }
  if (SUCCEEDED(hr)) {
    hr = WriteStringValue(serverKey, nullptr, modulePath);
  }
  if (SUCCEEDED(hr)) {
    hr = WriteStringValue(serverKey, L"ThreadingModel", L"Both");
  }

  if (serverKey) {
    RegCloseKey(serverKey);
  }
  RegCloseKey(classKey);
  return hr;
}

void UnregisterComClass() {
  wchar_t clsidString[64]{};
  if (StringFromGUID2(CLSID_HevcVideoExtension, clsidString, ARRAYSIZE(clsidString)) == 0) {
    return;
  }
  const std::wstring root = L"Software\\Classes\\CLSID\\" + std::wstring(clsidString);
  RegDeleteTreeW(HKEY_LOCAL_MACHINE, root.c_str());
}

HRESULT RegisterMft() {
  MFT_REGISTER_TYPE_INFO inputTypes[] = {
    { MFMediaType_Video, MFVideoFormat_HEVC },
    { MFMediaType_Video, MFVideoFormat_HEVC_ES }
  };
  MFT_REGISTER_TYPE_INFO outputTypes[] = {
    { MFMediaType_Video, MFVideoFormat_NV12 },
    { MFMediaType_Video, MFVideoFormat_P010 }
  };

  return MFTRegister(CLSID_HevcVideoExtension,
                     MFT_CATEGORY_VIDEO_DECODER,
                     const_cast<LPWSTR>(kFriendlyName),
                     MFT_ENUM_FLAG_SYNCMFT,
                     ARRAYSIZE(inputTypes), inputTypes,
                     ARRAYSIZE(outputTypes), outputTypes,
                     nullptr);
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = instance;
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllCanUnloadNow() {
  return (g_objectCount.load() == 0 && g_lockCount.load() == 0) ? S_OK : S_FALSE;
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllGetClassObject(REFCLSID clsid,
                                                                    REFIID riid,
                                                                    void** object) {
  if (!object) {
    return E_POINTER;
  }
  *object = nullptr;
  if (clsid != CLSID_HevcVideoExtension) {
    return CLASS_E_CLASSNOTAVAILABLE;
  }

  auto* factory = new (std::nothrow) HevcClassFactory();
  if (!factory) {
    return E_OUTOFMEMORY;
  }
  const HRESULT hr = factory->QueryInterface(riid, object);
  factory->Release();
  return hr;
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllRegisterServer() {
  HRESULT hr = RegisterComClass();
  if (FAILED(hr)) {
    return hr;
  }
  hr = RegisterMft();
  if (FAILED(hr)) {
    UnregisterComClass();
  }
  return hr;
}

extern "C" HRESULT __declspec(dllexport) WINAPI DllUnregisterServer() {
  const HRESULT hr = MFTUnregister(CLSID_HevcVideoExtension);
  UnregisterComClass();
  return SUCCEEDED(hr) || hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? S_OK : hr;
}

STDMETHODIMP HevcClassFactory::QueryInterface(REFIID riid, void** object) {
  if (!object) return E_POINTER;
  *object = nullptr;
  if (riid == IID_IUnknown || riid == IID_IClassFactory) {
    *object = static_cast<IClassFactory*>(this);
    AddRef();
    return S_OK;
  }
  return E_NOINTERFACE;
}

HevcClassFactory::HevcClassFactory() {
  ++g_objectCount;
}

HevcClassFactory::~HevcClassFactory() {
  --g_objectCount;
}

STDMETHODIMP_(ULONG) HevcClassFactory::AddRef() {
  return ++m_refCount;
}

STDMETHODIMP_(ULONG) HevcClassFactory::Release() {
  const ULONG count = --m_refCount;
  if (count == 0) delete this;
  return count;
}

STDMETHODIMP HevcClassFactory::CreateInstance(IUnknown* outer, REFIID riid, void** object) {
  if (outer) return CLASS_E_NOAGGREGATION;
  if (!object) return E_POINTER;
  *object = nullptr;

  auto* decoder = new (std::nothrow) HevcDecoder();
  if (!decoder) return E_OUTOFMEMORY;
  const HRESULT hr = decoder->QueryInterface(riid, object);
  decoder->Release();
  return hr;
}

STDMETHODIMP HevcClassFactory::LockServer(BOOL lock) {
  if (lock) {
    ++g_lockCount;
  } else {
    --g_lockCount;
  }
  return S_OK;
}
