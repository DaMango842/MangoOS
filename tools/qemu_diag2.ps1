# tools/qemu_diag2.ps1 - try 0.0.0.0 bind + report
$err = "$env:TEMP\opencode\qemu_err2.txt"
if (Test-Path $err) { Remove-Item $err -Force }
$p = Start-Process 'D:\Softwares\qemu\qemu-system-i386.exe' `
    -ArgumentList '-display', 'none', '-m', '16',
    '-serial', 'tcp:0.0.0.0:5559,server,nowait' `
    -PassThru -WindowStyle Hidden -RedirectStandardError $err
Start-Sleep -Seconds 4
Write-Output ("alive=" + (-not $p.HasExited))
Get-Content $err -ErrorAction SilentlyContinue | ForEach-Object { Write-Output $_ }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue