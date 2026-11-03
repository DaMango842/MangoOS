# tools/accudiag.ps1 - run AccuracyCoin headlessly, capture serial to
# file, wait for the hang, report the last DBG2 state dumps.
param([int]$WaitSec = 180)
$ErrorActionPreference = 'Stop'
$capPath = "$env:TEMP\opencode\accu_cap.txt"
$img = 'D:\Project\MangoOS\image\disk.img'
$tmp = "$env:TEMP\opencode"

if (Test-Path $capPath) { Remove-Item $capPath -Force }
Get-Process qemu-system-i386 -ErrorAction SilentlyContinue | Stop-Process -Force

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = 'D:\Softwares\qemu\qemu-system-i386.exe'
$psi.Arguments = @(
    "-hda `"$img`"", '-rtc base=localtime', '-boot', 'c',
    '-serial', "file:$(($capPath -replace '\\','/'))",
    '-display', 'none', '-m', '16'
) -join ' '
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Output "qemu pid=$($proc.Id)  serial=file -> $capPath"
Write-Output "waiting ${WaitSec}s..."
Start-Sleep -Seconds $WaitSec
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800

$lines = Get-Content $capPath -ErrorAction SilentlyContinue
Write-Output ("total lines=" + $lines.Count)

# show last 30 lines (should contain DBG2 dumps near the hang point)
Write-Output "--- last 30 lines ---"
$lines | Select-Object -Last 30

# extract all DBG2 lines for timeline
$dbg = $lines | Where-Object { $_ -match '^DBG2' }
Write-Output ("DBG2 count=" + $dbg.Count)
if ($dbg.Count -gt 0) {
    Write-Output "--- last 5 DBG2 ---"
    $dbg | Select-Object -Last 5
}