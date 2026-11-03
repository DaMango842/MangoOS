$qemu = "D:\Softwares\qemu\qemu-system-i386.exe"
$dbg = "D:\Project\MangoOS\probe\dbgcon.txt"
if (Test-Path $dbg) { Remove-Item $dbg -Force }
$args = @(
  "-drive","file=D:\Project\MangoOS\image\disk.img,format=raw",
  "-rtc","base=localtime","-boot","c","-m","16",
  "-vga","std","-display","none",
  "-debugcon","file:$dbg",
  "-serial","null")
Write-Output "running: $qemu $($args -join ' ')"
$p = Start-Process -FilePath $qemu -PassThru -ArgumentList $args `
     -RedirectStandardOutput "D:\Project\MangoOS\probe\q_out.txt" `
     -RedirectStandardError  "D:\Project\MangoOS\probe\q_err.txt"
Start-Sleep -Seconds 6
Write-Output "alive: $(-not $p.HasExited)"
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
Start-Sleep -Milliseconds 800
Write-Output "--- stderr ---"
if (Test-Path "D:\Project\MangoOS\probe\q_err.txt") { Get-Content "D:\Project\MangoOS\probe\q_err.txt" }
Write-Output "--- debugcon ---"
if (Test-Path $dbg) {
  $raw = [System.IO.File]::ReadAllBytes($dbg)
  Write-Output "bytes: $($raw.Length)"
  $sb = New-Object System.Text.StringBuilder
  foreach ($b in $raw) {
    if ($b -ge 32 -and $b -lt 127) { [void]$sb.Append([char]$b) } else { [void]$sb.Append("`n") }
  }
  Write-Output $sb.ToString()
} else { Write-Output "MISSING debugcon file" }