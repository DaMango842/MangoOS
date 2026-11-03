# tools/minirender.ps1 - offline replica of fc_ppu.c background math for a
# horizontal strip, using the ROM CHR + fcv dumps, to predict what the
# emulator SHOULD draw there (vs what the screendump shows).
param(
    [int]$YFrom = 44,
    [int]$YTo = 84,
    [int]$XFrom = 116,
    [int]$XTo = 188,
    [int]$Sx = 28,
    [int]$Sy = 0,
    [int]$Ctrl = 0x94
)
$ErrorActionPreference = 'Stop'

# ---- load ROM CHR ----
$rom = [IO.File]::ReadAllBytes('D:\Project\MangoOS\roms\Super Mario Bros. (W) [!].nes')
$chrBase = 0x8010

# ---- load latest fcv dumps from oam.log ----
$log = Get-Content "$env:TEMP\opencode\oam.log"
$vram = New-Object 'System.Collections.Generic.Dictionary[string,string]'
$table = -1; $off = -1
foreach ($ln in $log) {
    if ($ln -match '^V([01]):([0-9A-F]{4}):(.*)$') {
        $table = [int]$Matches[1]
        $off = [Convert]::ToInt32($Matches[2], 16)
        $bytes = [regex]::Matches($Matches[3], '[0-9A-F]{2}')
        foreach ($m in $bytes) {
            $vram["$table,$off"] = $m.Value
            $off++
        }
    }
    if ($ln -match '^A([01]):(.*)$') {
        # attribute bytes follow on continuation lines starting with spaces
    }
}
# attribute continuation lines: collect hex pairs after A0:/A1: header lines
$table = 0; $attrOn = $false
foreach ($ln in $log) {
    if ($ln -match '^A([01]):\s*(.*)$') {
        $table = [int]$Matches[1]
        $off = 0x3C0
        $attrOn = $true
        $rest = $Matches[2]
    } elseif ($attrOn -and $ln -match '^\s{2,}(.*)$') {
        $rest = $Matches[1]
    } else { $attrOn = $false; continue }
    foreach ($m in [regex]::Matches($rest, '[0-9A-F]{2}')) {
        $vram["$table,$off"] = $m.Value
        $off++
        if ($off -ge 0x400) { $attrOn = $false }
    }
}

# ---- palette from fcp dump (PAL section: k:v pairs) ----
$pal = New-Object 'System.Collections.Generic.Dictionary[int,int]'
$inPal = $false
foreach ($ln in $log) {
    if ($ln -match '^PAL') { $inPal = $true; continue }
    if ($inPal) {
        $ms = [regex]::Matches($ln, '(\d+):([0-9A-F]{2})')
        if ($ms.Count -eq 0) { if ($pal.Count -gt 0) { break } else { continue } }
        foreach ($m in $ms) {
            $pal[[int]$m.Groups[1].Value] = [Convert]::ToInt32($m.Groups[2].Value, 16)
        }
    }
}
$nesPal = @{ }
$rgb = @{
    0x00 = '7C7C7C'; 0x01 = '0000FC'; 0x02 = '0000BC'; 0x03 = '4428BC'
    0x04 = '940084'; 0x05 = 'A80020'; 0x06 = 'A81000'; 0x07 = '881400'
    0x08 = '503000'; 0x09 = '007800'; 0x0A = '006800'; 0x0B = '005800'
    0x0C = '004058'; 0x0D = '000000'; 0x0E = '000000'; 0x0F = '000000'
    0x10 = 'BCBCBC'; 0x11 = '0078F8'; 0x12 = '0058F8'; 0x13 = '6844FC'
    0x14 = 'D800CC'; 0x15 = 'E40058'; 0x16 = 'F83800'; 0x17 = 'E45C10'
    0x18 = 'AC7C00'; 0x19 = '00B800'; 0x1A = '00A800'; 0x1B = '00A844'
    0x1C = '008888'; 0x20 = 'F8F8F8'; 0x21 = '3CBCFC'; 0x22 = '6888FC'
    0x23 = '9878F8'; 0x24 = 'F878F8'; 0x25 = 'F85898'; 0x26 = 'F0D0B0'
    0x27 = 'FCA044'; 0x28 = 'F8B800'; 0x29 = 'B8F818'; 0x2A = '58D854'
    0x2B = '58F898'; 0x2C = '00E8D8'; 0x30 = 'FCFCFC'; 0x31 = 'A4E4FC'
    0x32 = 'B8B8F8'; 0x33 = 'D8B8F8'
}

function NtByte([int]$t, [int]$a) {
    $k = "$t,$a"
    if ($vram.ContainsKey($k)) { return [Convert]::ToInt32($vram[$k], 16) }
    return -1
}

$bgTbl = (($Ctrl -band 0x10) -ne 0)
$mirrorH = 1                     # SMB1 vertical mirroring
$baseNN = $Ctrl -band 3

for ($y = $YFrom; $y -le $YTo; $y += 2) {
    $rowOut = ''
    for ($x = $XFrom; $x -le $XTo; $x += 4) {
        $wx = $Sx + $x
        $wy = $Sy + $y
        $trow = $wy -shr 3
        $tcol = $wx -shr 3
        $xoff = $wx -band 7
        $yoff = $wy -band 7

        if ($mirrorH) { $tbl = ($baseNN -bxor (($tcol -shr 5) -band 1)) -band 1 }
        else { $tbl = (($baseNN -shr 1) -bxor (($trow -shr 5) -band 1)) -band 1 }

        $ntIdx = $tbl * 0x400 + ($trow -band 31) * 32 + ($tcol -band 31)
        $tile = NtByte $tbl $ntIdx
        $chrOff = $tile * 16 + $(if ($bgTbl) { 0x1000 } else { 0 })
        $lo = $rom[$chrBase + $chrOff + $yoff]
        $hi = $rom[$chrBase + $chrOff + $yoff + 8]
        $bit = 7 - $xoff
        $pixIdx = ((($hi -shr $bit) -band 1) -shl 1) -bor (($lo -shr $bit) -band 1)

        $areg = $tbl * 0x400 + 0x3C0 + ([math]::Floor(($trow -band 31) / 4)) * 8 + [math]::Floor(($tcol -band 31) / 4)
        $q = ((($trow -band 31) -band 2) * 2) + (($tcol -band 31) -band 2)
        $ab = NtByte $tbl $areg
        $attr = 0
        if ($ab -ge 0) { $attr = ($ab -shr $q) -band 3 }

        $palIdx = $attr * 4 + $pixIdx
        if ($pixIdx -eq 0) { $nes = $pal[0] } else { $nes = $pal[$palIdx] }
        if ($null -eq $nes) { $col = '??????' } else { $col = $nesPal[[int]$nes] }
        $rowOut += $col + ' '
    }
    Write-Output ("y={0:00}: {1}" -f $y, $rowOut)
}
