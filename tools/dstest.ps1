# tools/dstest.ps1 - check whether the dsound audiodev initializes
$out = "$env:TEMP\opencode\ds.txt"
Set-Content -Path $out -Value 'start'
$p = Start-Process 'D:\Softwares\qemu\qemu-system-i386.exe' `
    -ArgumentList '-audiodev', 'dsound,id=t0', '-display', 'none' `
    -PassThru -WindowStyle Hidden
Start-Sleep -Seconds 4
if ($p.HasExited) {
    Add-Content $out ("DSOUND_DIED exitcode=" + $p.ExitCode)
} else {
    Stop-Process -Id $p.Id -Force
    Add-Content $out 'DSOUND_ALIVE'
}
Get-Content $out