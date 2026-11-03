# tools/titlediff.ps1 - find the block in ppu.bin whose nametable looks
# like the SMB title screen, then diff rows 2..11 against our fcv dump.
param(
    [string]$Log = "$env:TEMP\opencode\oam.log"
)
$b = [IO.File]::ReadAllBytes('D:\Project\MangoOS\tools\fc\ppu.bin')
$blockSize = 0x3020
$cir0 = 0x2000          # CIRAM offset inside a block
$nBlocks = [math]::Floor($b.Length / $blockSize)

# ---- our NT0 from log ----
$ours = @{}
foreach ($ln in (Get-Content $Log)) {
    if ($ln -match '^V0:([0-9A-F]{4}):(.*)$') {
        $off = [Convert]::ToInt32($Matches[1], 16)
        foreach ($m in [regex]::Matches($Matches[2], '[0-9A-F]{2}')) {
            $ours[$off] = $m.Value
            $off++
        }
    }
}

# ---- score every block: how closely does row4 match ours? ----
$bestScore = -1; $bestBlock = -1
for ($k = 0; $k -lt $nBlocks; $k++) {
    $base = $k * $blockSize + $cir0
    $score = 0
    for ($c = 0; $c -lt 32; $c++) {
        $real = '{0:X2}' -f $b[$base + 4 * 32 + $c]
        if ($ours.ContainsKey(4 * 32 + $c) -and $ours[4 * 32 + $c] -eq $real) { $score++ }
    }
    if ($score -gt $bestScore) { $bestScore = $score; $bestBlock = $k }
}
Write-Output ("best title-candidate block=" + $bestBlock + " row4 matches=" + $bestScore + "/32")

$base = $bestBlock * $blockSize + $cir0
for ($r = 2; $r -le 7; $r++) {
    # compact run-length of real row
    $rl = ''
    $prev = ''; $cnt = 0
    for ($c = 0; $c -lt 32; $c++) {
        $v = '{0:X2}' -f $b[$base + $r * 32 + $c]
        if ($v -eq $prev) { $cnt++ }
        else {
            if ($prev) { $rl += "$prev x$cnt | " }
            $prev = $v; $cnt = 1
        }
    }
    $rl += "$prev x$cnt"

    $ol = ''
    $prev = ''; $cnt = 0
    for ($c = 0; $c -lt 32; $c++) {
        $v = $(if ($ours.ContainsKey($r * 32 + $c)) { $ours[$r * 32 + $c] } else { '--' })
        if ($v -eq $prev) { $cnt++ }
        else {
            if ($prev) { $ol += "$prev x$cnt | " }
            $prev = $v; $cnt = 1
        }
    }
    $ol += "$prev x$cnt"
    Write-Output ("row $r REAL: " + $rl)
    Write-Output ("row $r OURS: " + $ol)
}