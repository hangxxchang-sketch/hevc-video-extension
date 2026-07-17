[CmdletBinding()]
param(
    [ValidateSet("Install", "Uninstall")]
    [string]$Action = "Install",
    [string]$BuildDir = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $candidateDirs = @(
        (Join-Path $PSScriptRoot "..\bin"),
        (Join-Path $PSScriptRoot "..\build\x64\Release")
    )
    $BuildDir = $candidateDirs | Where-Object {
        Test-Path (Join-Path $_ "HevcVideoExtension.dll")
    } | Select-Object -First 1
}

$dll = Join-Path $BuildDir "HevcVideoExtension.dll"
if (-not (Test-Path $dll)) {
    throw "找不到 $dll。請先用 Visual Studio 建置 Release x64。"
}

$regsvr = Join-Path $env:WINDIR "System32\regsvr32.exe"
if (-not (Test-Path $regsvr)) {
    throw "找不到 regsvr32.exe。"
}

if ($Action -eq "Install") {
    Start-Process -FilePath $regsvr -ArgumentList "/s", $dll -Verb RunAs -Wait
    Write-Host "HEVC Media Foundation decoder 已註冊：$dll"
    Write-Host "可執行 scripts\Test-HEVC.ps1 或 build\x64\Release\HevcMftProbe.exe 驗證。"
} else {
    Start-Process -FilePath $regsvr -ArgumentList "/s", "/u", $dll -Verb RunAs -Wait
    Write-Host "HEVC Media Foundation decoder 已解除註冊。"
}
