# tools/ppm2png.ps1 - convert a QEMU screendump (P6 ppm) to png
param(
    [Parameter(Mandatory = $true)][string]$Ppm,
    [string]$Png
)
$ErrorActionPreference = 'Stop'
if (-not $Png) { $Png = [IO.Path]::ChangeExtension($Ppm, '.png') }

$b = [IO.File]::ReadAllBytes($Ppm)
if (([char]$b[0]) -ne 'P' -or ([char]$b[1]) -ne '6') { throw 'not a P6 ppm' }

$p = 2; $vals = @(); $tok = ''
while ($vals.Count -lt 3) {
    $ch = [char]$b[$p]
    if ($ch -eq '#') { while (([char]$b[$p]) -ne "`n") { $p++ }; continue }
    if ($ch -eq ' ' -or $ch -eq "`n" -or $ch -eq "`r" -or $ch -eq "`t") {
        if ($tok) { $vals += [int]$tok; $tok = '' }
        $p++
    } else { $tok += $ch; $p++ }
}
$p++
$w = [int]$vals[0]; $h = [int]$vals[1]

Add-Type -AssemblyName PresentationCore
$stride = $w * 3
$len = $h * $stride
$pix = New-Object byte[] $len
[Array]::Copy($b, $p, $pix, 0, [Math]::Min($len, $b.Length - $p))
$bs = [System.Windows.Media.Imaging.BitmapSource]::Create(
    $w, $h, 96, 96,
    [System.Windows.Media.PixelFormats]::Rgb24,
    $null, $pix, $stride)
$enc = New-Object System.Windows.Media.Imaging.PngBitmapEncoder
$enc.Frames.Add([System.Windows.Media.Imaging.BitmapFrame]::Create($bs))
$out = [IO.File]::Create($Png)
$enc.Save($out)
$out.Close()
Write-Output ("PNG saved {0}x{1} -> {2}" -f $w, $h, $Png)