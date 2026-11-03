# tools/palscan.ps1 - locate SMB1 background-palette signature inside a
# large binary (FCEUX state/dump) to figure out its internal layout.
$b = [IO.File]::ReadAllBytes('D:\Project\MangoOS\tools\fc\ppu.bin')
Write-Output ("size=" + $b.Length)

$hits = @()
for ($i = 0; $i -lt $b.Length - 16; $i++) {
    # bg block: ?,29,1A,?, ?,36,17,?, ?,30,21,?, ?,27,17,?
    if ($b[$i + 1] -eq 0x29 -and $b[$i + 2] -eq 0x1A -and
        $b[$i + 5] -eq 0x36 -and $b[$i + 6] -eq 0x17 -and
        $b[$i + 9] -eq 0x30 -and $b[$i + 10] -eq 0x21 -and
        $b[$i + 13] -eq 0x27) {
        $hits += $i
        if ($hits.Count -ge 12) { break }
    }
}
foreach ($h in $hits) {
    $s = ''
    for ($j = 0; $j -lt 16; $j++) { $s += ('{0:X2} ' -f $b[$h + $j]) }
    Write-Output ("@0x{0:X6}: {1}" -f $h, $s)
}
if ($hits.Count -eq 0) {
    Write-Output 'no bg-palette signature found'
    # fallback: show first 64 bytes to identify container
    $s = ''
    for ($j = 0; $j -lt 48; $j++) { $s += ('{0:X2} ' -f $b[$j]) }
    Write-Output ('head48: ' + $s)
}