# tools/oampeek.ps1 - list OAM entries whose Y falls in a range, plus
# the first 8 sprites, from an fclog containing the fco dump.
param(
    [string]$Log = "$env:TEMP\opencode\oam.log",
    [int]$YMin = 0,
    [int]$YMax = 255
)
$lines = Get-Content $Log
$inOam = $false
foreach ($ln in $lines) {
    if ($ln -match '^OAM') { $inOam = $true; continue }
    if ($inOam -and $ln -match '^([0-9A-F]{2}): ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2}) ([0-9A-F]{2})\s*$') {
        $idx = [Convert]::ToInt32($Matches[1], 16)
        $y = [Convert]::ToInt32($Matches[2], 16)
        $t = $Matches[3]
        $a = $Matches[4]
        $x = [Convert]::ToInt32($Matches[5], 16)
        if ($idx -lt 8 -or ($y -ge $YMin -and $y -le $YMax)) {
            Write-Output ("slot {0:00}: Y={1:X2} tile={2} attr={3} X={4:X2}" -f $idx, $y, $t, $a, $x)
        }
    }
}