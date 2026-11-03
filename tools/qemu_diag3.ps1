# tools/qemu_diag3.ps1 - qemu with named-pipe serial, capture stderr
$err = "$env:TEMP\opencode\qemu_err3.txt"
if (Test-Path $err) { Remove-Item $err -Force }
$p = Start-Process 'D:\Softwares\qemu\qemu-system-i386.exe' `
    -ArgumentList '-display', 'none', '-m', '16',
    '-serial', 'pipe:mangos' `
    -PassThru -WindowStyle Hidden -RedirectStandardError $err
Start-Sleep -Seconds 3
Write-Output ("alive_before_connect=" + (-not $p.HasExited))

# connect as client (duplex)
$client = New-Object System.IO.Pipes.NamedPipeClientStream('.', 'mangos', [System.IO.Pipes.PipeDirection]::InOut)
try {
    $client.Connect(3000)
    Write-Output "client connected"
} catch {
    Write-Output ("client connect failed: " + $_.Exception.Message)
}
Start-Sleep -Seconds 3
Write-Output ("alive_after_connect=" + (-not $p.HasExited))
Get-Content $err -ErrorAction SilentlyContinue | ForEach-Object { Write-Output $_ }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyDispose
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue