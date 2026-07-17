# HEVC Video Extension for Windows

這是一個 Windows Media Foundation HEVC/H.265 解碼延伸模組。它以 C++ 實作同步 MFT，將 HEVC Annex-B 輸入解碼成 NV12 或 P010，並透過 FFmpeg 的 LGPL 元件完成軟體解碼。若 Windows 顯示卡與 Media Foundation pipeline 支援硬體解碼，系統原生解碼器仍可能優先被選用。

## 目前支援

- Windows 10/11 x64；CMake 也預留 ARM64 preset。
- HEVC Main、Main 10（輸出 P010 需搭配 10-bit FFmpeg frame）。
- `MFVideoFormat_HEVC`、`MFVideoFormat_HEVC_ES` 輸入。
- `MFVideoFormat_NV12`、`MFVideoFormat_P010` 輸出。
- 以 `MFTRegister` 註冊到 `MFT_CATEGORY_VIDEO_DECODER`。

這不是 Microsoft Store 的官方 `HEVC Video Extensions` 套件，也不會繞過 HEVC 專利授權。若要公開散布，請先完成 FFmpeg LGPL、HEVC 專利池與你所在地的法律審查。

## 在 Windows 建置

需求：Visual Studio 2022（Desktop C++）、Windows 10/11 SDK、CMake 3.25+、vcpkg。

```powershell
git clone https://github.com/microsoft/vcpkg.git C:\dev\vcpkg
C:\dev\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\dev\vcpkg"

cmake --preset windows-x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build\x64 --config Release
```

如果不使用 vcpkg，請把 `FFMPEG_ROOT` 指到包含 `include\libavcodec\avcodec.h`、`lib\avcodec.lib` 與對應 DLL 的 FFmpeg development/shared build：

```powershell
cmake --preset windows-x64 -DFFMPEG_ROOT=C:\dev\ffmpeg
cmake --build build\x64 --config Release
```

建置後請把 FFmpeg 的 `avcodec-*.dll`、`avutil-*.dll`、`swscale-*.dll` 複製到 `HevcVideoExtension.dll` 同一個資料夾。

## GitHub Windows 下載版

此 repository 已包含 GitHub Actions。每次 push、Pull Request 或手動執行 `Build Windows HEVC Extension` workflow，都會在 Actions 的 Artifacts 產生 `HevcVideoExtension-windows-x64.zip`。若建立 `v0.1.0` 這類 tag，workflow 也會自動建立 GitHub Release 並附上 ZIP。

下載 ZIP 後解壓縮，在系統管理員 PowerShell 執行：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\Register-HEVC.ps1 -Action Install
.\scripts\Test-HEVC.ps1
```

## 安裝與測試

註冊 MFT 會寫入 Windows 的系統 COM/MFT 登錄，需要系統管理員權限：

```powershell
Set-ExecutionPolicy -Scope Process Bypass
.\scripts\Register-HEVC.ps1 -Action Install -BuildDir .\build\x64\Release
.\scripts\Test-HEVC.ps1 -ProbePath .\build\x64\Release\HevcMftProbe.exe
```

若要移除：

```powershell
.\scripts\Register-HEVC.ps1 -Action Uninstall -BuildDir .\build\x64\Release
```

測試工具會透過 `MFTEnumEx` 列出目前系統可用的 HEVC 解碼器；若清單中出現 `HEVC Video Extension (FFmpeg)`，代表本模組已被 Media Foundation 找到。請再用 Windows Media Player、Movies & TV 或你的 Media Foundation 播放器開啟 `.mp4`/`.mkv` 做實際播放測試。

## 重要限制

1. 本機是 macOS，無法在這裡執行 Windows SDK、MSVC、`regsvr32` 或真正的播放驗證；程式碼與 CMake 需在 Windows 上完成最後編譯。
2. MFT 使用同步處理模型，適合先驗證功能；若要正式產品化，應加入硬體 DXVA/D3D11 surface、非同步 MFT、色彩資訊、動態解析度變更、flush/drain 壓力測試與 crash isolation。
3. 這個版本預期輸入 media type 已包含 `MF_MT_FRAME_SIZE`；沒有正確 frame size 的來源會被拒絕。
4. Windows 內建 HEVC 解碼器可能優先於本模組，這是 Media Foundation 的正常選擇行為。

## 第三方元件

本專案只直接使用 FFmpeg 的 `libavcodec`、`libavutil` 與 `libswscale`。請在散布版本中附上對應的 FFmpeg LGPL 授權文字、版本資訊與原始碼取得方式，並確認建置設定沒有引入 GPL-only 元件。
