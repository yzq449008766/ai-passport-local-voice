# 后台启动语音网关（Windows）
$env:PYTHONIOENCODING = "utf-8"
$dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$logDir = Join-Path $dir "logs"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$py = & python -c "import sys; print(sys.executable)"
Start-Process -FilePath $py -ArgumentList (Join-Path $dir "gateway.py") `
    -WorkingDirectory $dir -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $logDir "gateway.log") `
    -RedirectStandardError (Join-Path $logDir "gateway.err.log")
Start-Sleep -Seconds 4
try {
    $h = Invoke-RestMethod "http://127.0.0.1:8000/api/health" -TimeoutSec 5
    Write-Host ("gateway up: " + ($h | ConvertTo-Json -Compress))
} catch {
    Write-Host ("health fail: " + $_.Exception.Message)
    Get-Content (Join-Path $logDir "gateway.err.log") -Tail 20 -ErrorAction SilentlyContinue
}
