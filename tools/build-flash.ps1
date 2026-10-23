# AI Passport Local Voice —— 构建 + 合并 + 刷机脚本（Windows / ESP-IDF 5.5.3）
param(
    [string]$Port = "COM5",
    [string]$EspIdf = "E:\esp\esp-idf-v5.5.3",
    [string]$Repo = (Get-Location).Path
)

$ErrorActionPreference = "Stop"
$env:PYTHONIOENCODING = "utf-8"

# 1) 定位 IDF venv python（export.ps1 后的 python 常被劫持，显式用 venv 的）
$venvPy = Join-Path $env:USERPROFILE ".espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe"
if (-not (Test-Path $venvPy)) {
    Write-Host "未找到 IDF venv python: $venvPy" -ForegroundColor Red
    exit 1
}
$idfPy = Join-Path $EspIdf "tools\idf.py"

Write-Host "==> build ($Repo)" -ForegroundColor Cyan
Push-Location $Repo
try {
    & $venvPy $idfPy build
    if ($LASTEXITCODE -ne 0) { throw "build failed" }

    Write-Host "==> merge-bin" -ForegroundColor Cyan
    & $venvPy $idfPy merge-bin
    if ($LASTEXITCODE -ne 0) { throw "merge-bin failed" }

    Copy-Item -LiteralPath "build\merged-binary.bin" -Destination "build\FoloToy-AI-Passport-full.bin" -Force

    Write-Host "==> flash $Port (full image from 0x0)" -ForegroundColor Cyan
    & $venvPy -m esptool --chip esp32c3 -p $Port -b 460800 `
        --before default_reset --after hard_reset write_flash `
        --flash_mode dio --flash_size 8MB --flash_freq 80m `
        0x0 "build\FoloToy-AI-Passport-full.bin"
    if ($LASTEXITCODE -ne 0) { throw "flash failed" }

    Write-Host "==> OK. 刷入完成。" -ForegroundColor Green
} finally {
    Pop-Location
}
