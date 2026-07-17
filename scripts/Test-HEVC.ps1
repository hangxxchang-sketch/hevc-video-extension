[CmdletBinding()]
param(
    [string]$ProbePath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ProbePath)) {
    $candidatePaths = @(
        (Join-Path $PSScriptRoot "..\bin\HevcMftProbe.exe"),
        (Join-Path $PSScriptRoot "..\build\x64\Release\HevcMftProbe.exe")
    )
    $ProbePath = $candidatePaths | Where-Object { Test-Path $_ } | Select-Object -First 1
}

if (-not (Test-Path $ProbePath)) {
    throw "找不到 $ProbePath。請先建置專案。"
}

& $ProbePath
if ($LASTEXITCODE -ne 0) {
    throw "Windows Media Foundation 沒有成功列出 HEVC 解碼器。"
}
