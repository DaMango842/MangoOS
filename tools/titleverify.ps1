# tools/titleverify.ps1 - full-frame offline replica of the emulator's
# background math using OUR fcv/fcp dumps + ROM CHR, compared per 16x16
# block against the actual screendump; prints every diverging block.
param(
    [string]$Ppm = "$env:TEMP\opencode\shot.ppm",
    [int]$Ctrl = 0x94,
    [int]$Sx = 0,
    [int]$Sy = 0
)
$ErrorActionPreference = 'Stop'

$rom = [IO.File]::ReadAllBytes('D:\Project\MangoOS\roms\Super Mario Bros. (W) [!].nes')
$chrBase = 0x8010
$log = Get-Content "$env:TEMP\opencode\oam.log"

# ---- VRAM ----
$vram = @{}
$table = 0
foreach ($ln in $log) {
    if ($ln -match '^V([01]):([0-9A-F]{4}):(.*)$') {
        $t = [int]$Matches[1]
        $off = [Convert]::ToInt32($Matches[2], 16)
        foreach ($m in [regex]::Matches($Matches[3], '[0-9A-F]{2}')) {
            $vram["$t,$off"] = $m.Value
            $off++
        }
    }
}
# ---- palette ----
$pal = @{}
$inPal = $false
foreach ($ln in $log) {
    if ($ln -match '^PAL') { $inPal = $true; continue }
    if ($inPal) {
        $ms = [regex]::Matches($ln, '(\d+):([0-9A-F]{2})')
        if ($ms.Count -eq 0) { if ($pal.Count -gt 0) { break } else { continue } }
        foreach ($m in $ms) { $pal[[int]$m.Groups[1].Value] = [Convert]::ToInt32($m.Groups[2].Value, 16) }
    }
}
Write-Output ("palette entries=" + $pal.Count)

# ---- NES palette (subset covering SMB values incl 0F/21/22/30/00/16) ----
$rgb = @{
    0 = '7C7C7C'; 1 = '0000FC'; 2 = '0000BC'; 3 = '4428BC'; 4 = '940084'
    5 = 'A80020'; 6 = 'A81000'; 7 = '881400'; 8 = '503000'; 9 = '007800'
    10 = '006800'; 11 = '005800'; 12 = '004058'; 13 = '000000'; 14 = '000000'; 15 = '000000'
    16 = 'BCBCBC'; 17 = '0078F8'; 18 = '0058F8'; 19 = '6844FC'; 20 = 'D800CC'
    21 = '3CBCFC'; 22 = '6888FC'; 23 = '9878F8'; 24 = 'F878F8'; 25 = 'F85898'
    26 = 'F0D0B0'; 27 = 'FCA044'; 28 = 'F8B800'; 29 = 'B8F818'; 30 = '58D854'
    31 = '58F898'; 32 = '00E8D8'; 33 = '787878'
    48 = 'FCFCFC'; 49 = 'A4E4FC'; 50 = 'B8B8F8'; 51 = 'D8B8F8'
    52 = 'F8B8F8'; 53 = 'F8A4C0'; 54 = 'F0D0B0'; 55 = 'FCE0A8'
    56 = 'F8D878'; 57 = 'D8F878'; 58 = 'B8F8B8'; 59 = 'B8F8D8'
}

function NtByte([int]$t, [int]$a) {
    $k = "$t,$a"
    if ($vram.ContainsKey($k)) { return [Convert]::ToInt32($vram[$k], 16) }
    return -1
}

$bgTbl = (($Ctrl -band 0x10) -ne 0)
$baseNN = $Ctrl -band 3

$ppm = [IO.File]::ReadAllBytes($Ppm)

$mismatchBlocks = @{}
for ($y = 0; $y -lt 240; $y += 2) {
    for ($x = 0; $x -lt 256; $x += 2) {
        $wx = $Sx + $x; $wy = $Sy + $y
        $trow = $wy >> 3; $tcol = $wx >> 3
        $tbl = ($baseNN -bxor (($tcol -shr 5) -band 1)) -band 1   # vertical mirroring
        $ntIdx = $tbl * 0x400 + ($trow -band 31) * 32 + ($tcol -band 31)
        $tileB = NtByte $tbl $ntIdx
        if ($tileB -lt 0) { continue }
        $chrOff = $tileB * 16 + $(if ($bgTbl) { 0x1000 } else { 0 })
        $lo = $rom[$chrBase + $chrOff + ($wy -band 7)]
        $hi = $rom[$chrBase + $chrOff + ($wy -band 7) + 8]
        $bit = 7 - ($wx -band 7)
        $pixIdx = ((($hi -shr $bit) -band 1) -shl 1) -bor (($lo -shr $bit) -band 1)

        $areg = $tbl * 0x400 + 0x3C0 + ([math]::Floor(($trow -band 31) / 4)) * 8 + [math]::Floor(($tcol -band 31) / 4)
        $q = ((($trow -band 31) -band 2) * 2) + (($tcol -band 31) -band 2)
        $ab = NtByte $tbl $areg
        $attr = 0
        if ($ab -ge 0) { $attr = ($ab -shr $q) -band 3 }

        $palIdx = $attr * 4 + $pixIdx
        $nes = $(if ($pixIdx -eq 0) { $pal[0] } else { $pal[$palIdx] })
        if ($null -eq $nes) { continue }
        $expect = $rgb[[int]$nes]
        if ($null -eq $expect) { continue }

        # actual pixel from ppm (game gx,gy -> screen 144+2gx,60+2gy)
        $o = 15 + (60 + $y * 2) * 800 * 3 + (144 + $x * 2) * 3
        $act = '{0:X2}{1:X2}{2:X2}' -f $ppm[$o], $ppm[$o + 1], $ppm[$o + 2]
        if ($act -cne $expect) {
            $bk = [string][math]::Floor($x / 16) + ',' + [math]::Floor($y / 16)
            $mismatchBlocks[$bk] = 1 + $(if ($mismatchBlocks.ContainsKey($bk)) { $mismatchBlocks[$bk] } else { 0 })
        }
    }
}

Write-Output ("diverging 16x16 blocks: " + $mismatchBlocks.Count)
$list = @()
foreach ($k in $mismatchBlocks.Keys) { $list += $k }
$list = $list | Sort-Object { [int]($_ -split ',')[1] * 16 + [int]($_ -split ',')[0] }
Write-Output ("blocks(col,row): " + (($list | Select-Object -First 40) -join ' '))