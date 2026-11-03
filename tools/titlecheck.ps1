# tools/titlecheck.ps1 - analyze OUR title-screen nametable dumps:
# per-tile-row counts of blank-sky ($24) vs others, plus list non-sky
# cells so partial-draw stalls become obvious.
$log = "$env:TEMP\opencode\oam.log"
$lines = Get-Content $log

$table = -1
$rowNum = -1
$rows = @{}

foreach ($ln in $lines) {
    if ($ln -match '^V([01]):([0-9A-F]{4}):(.*)$') {
        $t = [int]$Matches[1]
        $off = [Convert]::ToInt32($Matches[2], 16)
        $rowNum = [int]($off / 32)
        $bytes = [regex]::Matches($Matches[3], '[0-9A-F]{2}')
        $arr = @()
        foreach ($m in $bytes) { $arr += $m.Value }
        if ($arr.Count -eq 32) { $rows["$t,$rowNum"] = $arr }
    }
}

foreach ($t in @(0, 1)) {
    Write-Output "=== NT$t ==="
    for ($r = 0; $r -lt 12; $r++) {
        $k = "$t,$r"
        if (-not $rows.ContainsKey($k)) { continue }
        $arr = $rows[$k]
        $n24 = 0
        $others = @{}
        foreach ($v in $arr) {
            if ($v -eq '24') { $n24++ } else { $others[$v] = 1 + $(if ($others.ContainsKey($v)) { $others[$v] } else { 0 }) }
        }
        $olist = ''
        foreach ($kk in $others.Keys) { $olist += "$kk(x$($others[$kk])) " }
        Write-Output ("  row {0:00}: sky={1} other=[{2}]" -f $r, $n24, $olist)
    }
    # attribute rows
    for ($r = 28; $r -lt 32; $r++) {
        $k = "$t,$r"
        if (-not $rows.ContainsKey($k)) { continue }
        $arr = $rows[$k]
        $distinct = @{}
        foreach ($v in $arr) { $distinct[$v] = 1 }
        $keys = @(); foreach ($kk in $distinct.Keys) { $keys += $kk }
        Write-Output ("  attr row {0:00}: distinct=({1})" -f $r, ($keys -join ','))
    }
}