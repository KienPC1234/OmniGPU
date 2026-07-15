param(
    [string]$WorkDir = "C:\Users\test\Downloads\GPU_Caps_Viewer_1.64.3.0\GPU_Caps_Viewer",
    [string]$HostAddr = "192.168.1.239",
    [int]$PortNum = 9443
)

Stop-Process -Name omnigpu_guest_test -Force -ErrorAction SilentlyContinue
Start-Sleep 1
Remove-Item (Join-Path $WorkDir "omnigpu_guest.log") -ErrorAction SilentlyContinue

$p = Start-Process -FilePath (Join-Path $WorkDir "omnigpu_guest_test.exe") -ArgumentList "$HostAddr $PortNum" -WorkingDirectory $WorkDir -NoNewWindow -PassThru
Start-Sleep 10

Write-Output "PID=$($p.Id)"
Write-Output "--- GUEST LOG ---"
Get-Content (Join-Path $WorkDir "omnigpu_guest.log") -ErrorAction SilentlyContinue
Write-Output "--- END GUEST LOG ---"
if ($p.HasExited) {
    Write-Output "ExitCode=$($p.ExitCode)"
} else {
    Write-Output "Process still running (handshake completed)"
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    Write-Output "Process killed after test"
}
