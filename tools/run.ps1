# Launch MangoOS in QEMU from the built hard disk image
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))

$img  = [System.IO.Path]::GetFullPath((Join-Path $root "image/disk.img"))

$qemu = (Get-Command "qemu-system-i386.exe" -ErrorAction SilentlyContinue)
if ($qemu) {
    $qemu = $qemu.Source
} else {
    $qemu = "D:\Softwares\qemu\qemu-system-i386.exe"
}
Write-Host "qemu: $qemu"
Write-Host "img : $img"
if (-not (Test-Path $qemu)) {
    Write-Host "qemu-system-i386.exe not found. Add it to PATH or fix the fallback path in run.ps1." -ForegroundColor Red
    exit 1
}
if (-not (Test-Path $img)) {
    Write-Host "disk.img not found: $img. Run 'make' first." -ForegroundColor Red
    exit 1
}

$qemuArgs = @(
    "-drive", ("file=" + $img + ",format=raw"),
    "-rtc", "base=localtime",
    "-boot", "c",
    "-m", "16",
    "-audiodev", "dsound,id=audio0",
    "-machine", "pcspk-audiodev=audio0",
    "-device", "sb16,audiodev=audio0"
)
Write-Host "cmd : $qemu $qemuArgs"
& $qemu @qemuArgs
