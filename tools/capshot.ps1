# tools/capshot.ps1 - passive screenshot capture: launch qemu with the
# serial port redirected to a file; the guest boots straight into the FC
# emulator and dumps its framebuffer twice (title + gameplay) with an
# FCSH01 magic.  No interactive input required.
param(
    [int]$WaitSec = 60
)
$ErrorActionPreference = 'Stop'
$root = 'D:\Project\MangoOS'
$img = "$root\image\disk.img"
$qemu = 'D:\Softwares\qemu\qemu-system-i386.exe'
$tmp = "$env:TEMP\opencode"
$capPath = "$tmp\cap.bin"

New-Item -ItemType Directory -Force -Path $tmp | Out-Null
if (Test-Path $capPath) { Remove-Item $capPath -Force }

Get-Process qemu-system-i386 -ErrorAction SilentlyContinue | Stop-Process -Force

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $qemu
$psi.Arguments = @(
    "-hda `"$img`"", '-rtc base=localtime', '-boot', 'c',
    '-serial', "file:$($capPath -replace '\\','/')",
    '-audiodev', 'dsound,id=audio0',
    '-machine', 'pcspk-audiodev=audio0',
    '-device', 'sb16,audiodev=audio0',
    '-display', 'none', '-m', '16'
) -join ' '
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Output ("qemu pid=$($proc.Id)  serial=file -> $capPath")
Write-Output ("waiting ${WaitSec}s for boot + title + gameplay captures...")

try {
    Start-Sleep -Seconds $WaitSec
} finally {
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
}

# ---- parse captures (retry: qemu may hold the handle briefly) ----
$b = $null
for ($try = 0; $try -lt 10; $try++) {
    try { $b = [IO.File]::ReadAllBytes($capPath); break }
    catch { Start-Sleep -Milliseconds 500 }
}
if ($null -eq $b) { throw "cannot read capture file" }
Write-Output ("capture size=" + $b.Length)
$magic = [byte[]](0x46, 0x43, 0x53, 0x48, 0x30, 0x31)   # FCSH01
$total = 256 * 240 * 4

$found = @()
for ($i = 0; $i -le $b.Length - 6 - $total; $i++) {
    if ($b[$i] -eq $magic[0] -and $b[$i+1] -eq $magic[1] -and
        $b[$i+2] -eq $magic[2] -and $b[$i+3] -eq $magic[3] -and
        $b[$i+4] -eq $magic[4] -and $b[$i+5] -eq $magic[5]) {
        $found += $i
        $i += $total   # skip past this payload
    }
}
Write-Output ("captures found=" + $found.Count)

function Save-Shot([int]$off, [string]$tag) {
    $w = 256; $h = 240
    $hdr = [Text.Encoding]::ASCII.GetBytes("P6`n$w $h`n255`n")
    $rgb = New-Object byte[] ($w * $h * 3)
    for ($px = 0; $px -lt ($w * $h); $px++) {
        $o = $off + 6 + $px * 4
        $rgb[$px * 3 + 0] = $b[$o + 2]
        $rgb[$px * 3 + 1] = $b[$o + 1]
        $rgb[$px * 3 + 2] = $b[$o]
    }
    $outBytes = New-Object byte[] ($hdr.Length + $rgb.Length)
    [Array]::Copy($hdr, 0, $outBytes, 0, $hdr.Length)
    [Array]::Copy($rgb, 0, $outBytes, $hdr.Length, $rgb.Length)
    $png = "$root\image\smb_$tag.png"
    $ppm = "$env:TEMP\opencode\smb_$tag.ppm"
    [IO.File]::WriteAllBytes($ppm, $outBytes)
    & (Join-Path $PSScriptRoot 'ppm2png.ps1') -Ppm $ppm -Png $png | Out-Null
    Write-Output ("saved: $png")
}

$n = 0
foreach ($f in $found) {
    Save-Shot $f $(switch ($n) { 0 { 'title' } default { 'game' } })
    $n++
}