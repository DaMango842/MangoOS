# tools/qemu_diag.ps1 - launch qemu with the probe args and report
# whether it stays alive, what stderr says, and whether the ports listen.
$err = "$env:TEMP\opencode\qemu_err.txt"
if (Test-Path $err) { Remove-Item $err -Force }
$p = Start-Process 'D:\Softwares\qemu\qemu-system-i386.exe' `
    -ArgumentList @(
        '-hda', 'D:\Project\MangoOS\image\disk.img',
        '-rtc', 'base=localtime', '-boot', 'c',
        '-serial', 'tcp:127.0.0.1:5557,server,nowait',
        '-monitor', 'tcp:127.0.0.1:4444,server,nowait',
        '-audiodev', 'dsound,id=audio0',
        '-machine', 'pcspk-audiodev=audio0',
        '-device', 'sb16,audiodev=audio0',
        '-display', 'none', '-m', '16'
    ) `
    -PassThru -WindowStyle Hidden `
    -RedirectStandardError $err
Start-Sleep -Seconds 5
$alive = -not $p.HasExited
Write-Output ("alive=" + $alive)
if (-not $alive) { Write-Output ("exitcode=" + $p.ExitCode) }
$listening = Get-NetTCPConnection -State Listen -ErrorAction SilentlyContinue |
    Where-Object { $_.LocalPort -in 4444, 5557 }
foreach ($l in $listening) {
    Write-Output ("LISTEN port=" + $l.LocalPort)
}
Write-Output "--- stderr ---"
Get-Content $err -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue