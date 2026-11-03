# tools/palcmp.ps1 - check palette stability across all blocks in the
# FCEUX multi-block PPU dump, and print CIRAM row samples.
$b = [IO.File]::ReadAllBytes('D:\Project\MangoOS\tools\fc\ppu.bin')
$blockSize = 0x3020
$palOffInBlock = 0x3000
$nBlocks = [math]::Floor($b.Length / $blockSize)
Write-Output ("blocks=" + $nBlocks)

$seen = @{}
for ($k = 0; $k -lt $nBlocks; $k++) {
    $p = $k * $blockSize + $palOffInBlock
    # signature-check this is really the SMB bg palette block
    if ($b[$p + 1] -ne 0x29) { continue }
    $s = ''
    for ($j = 0; $j -lt 32; $j++) { $s += ('{0:X2}' -f $b[$p + $j]) }
    $seen[$s] = 1 + $(if ($seen.ContainsKey($s)) { $seen[$s] } else { 0 })
}
Write-Output ("distinct palettes=" + $seen.Count)
foreach ($k in $seen.Keys) {
    $sp = ''
    for ($j = 0; $j -lt 32; $j += 2) { $sp += $k.Substring($j * 2, 2) + ' ' }
    Write-Output ("  x" + $seen[$k] + ": " + $sp)
}

# ---- CIRAM sample from block 0: rows 6-7 of both logical NTs ----
$cir0 = 0x2000
$row6 = ''
for ($c = 16; $c -le 26; $c++) {
    $row6 += ('{0:X2} ' -f $b[$cir0 + 6 * 32 + $c])
}
Write-Output ("blk0 V0 row6 cols16-26: " + $row6)
$row7 = ''
for ($c = 16; $c -le 26; $c++) {
    $row7 += ('{0:X2} ' -f $b[$cir0 + 7 * 32 + $c])
}
Write-Output ("blk0 V0 row7 cols16-26: " + $row7)
# attributes rows 4-7 -> attr byte row1 (offset 0x3C8..0x3CF inside NT0)
$attr = ''
for ($c = 0x3C8; $c -le 0x3CF; $c++) {
    $attr += ('{0:X2} ' -f $b[$cir0 + $c])
}
Write-Output ("blk0 attr row1 (3C8-3CF): " + $attr)