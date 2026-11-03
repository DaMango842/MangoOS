# tools/blackscan.ps1 - locate solid-black regions inside the captured
# SMB frame (game coords) and print collapsed bands so artifact tiles
# can be mapped to nametable cells.
$ppm = "$env:TEMP\opencode\shot.ppm"
$b = [IO.File]::ReadAllBytes($ppm)
$W = 800

$prevDesc = ''
$bandStart = -1
$bandEnd = -1

for ($gy = 0; $gy -lt 240; $gy++) {
    $desc = ''
    $run = -1
    for ($gx = 0; $gx -lt 256; $gx++) {
        $sx = 144 + $gx * 2
        $sy = 60 + $gy * 2
        $o = 15 + $sy * $W * 3 + $sx * 3
        $isB = ($b[$o] -eq 0) -and ($b[$o + 1] -eq 0) -and ($b[$o + 2] -eq 0)
        if ($isB) {
            if ($run -lt 0) { $run = $gx }
        } else {
            if ($run -ge 0) { $desc += "$run-$($gx - 1)|"; $run = -1 }
        }
    }
    if ($run -ge 0) { $desc += "$run-255|" }

    if ($desc -ne '') {
        if (($desc -eq $prevDesc) -and ($gy -eq $bandEnd + 1)) {
            $bandEnd = $gy
        } else {
            if ($bandStart -ge 0) {
                $t0 = [math]::Floor($bandStart / 8)
                $t1 = [math]::Floor($bandEnd / 8)
                Write-Output ("rows {0}-{1} (tiles {2}-{3}): {4}" -f $bandStart, $bandEnd, $t0, $t1, $prevDesc)
            }
            $bandStart = $gy
            $bandEnd = $gy
            $prevDesc = $desc
        }
    } else {
        if ($bandStart -ge 0) {
            $t0 = [math]::Floor($bandStart / 8)
            $t1 = [math]::Floor($bandEnd / 8)
            Write-Output ("rows {0}-{1} (tiles {2}-{3}): {4}" -f $bandStart, $bandEnd, $t0, $t1, $prevDesc)
            $bandStart = -1
            $prevDesc = ''
        }
    }
}
if ($bandStart -ge 0) {
    $t0 = [math]::Floor($bandStart / 8)
    $t1 = [math]::Floor($bandEnd / 8)
    Write-Output ("rows {0}-{1} (tiles {2}-{3}): {4}" -f $bandStart, $bandEnd, $t0, $t1, $prevDesc)
}
