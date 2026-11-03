# tools/ntpeek.ps1 - print nametable cells from an fclog that contains
# "V0:" / "V1:" dumps (fcv command output).
param(
    [string]$Log = "$env:TEMP\opencode\oam.log",
    [int]$Table = 0,
    [string]$RowList = "6,7,8,9",
    [int]$ColFrom = 24,
    [int]$ColTo = 34
)

$lines = Get-Content $Log
$tag = 'V' + $Table + ':'
$idx = 0
$rowVals = @{}

foreach ($ln in $lines) {
    if ($ln.StartsWith($tag)) {
        # format: V0:XXXX: hh hh ... (32 bytes)
        $parts = $ln.Split(' ')
        $byteIdx = 0
        foreach ($p in $parts) {
            if ($p -match '^[0-9A-F]{2}$') {
                $rowVals[[string]$byteIdx] = $p
                $byteIdx++
            }
        }
        $rowNum = $idx
        if ($RowList -split ',' | ForEach-Object { [int]$_ } | Where-Object { $_ -eq $rowNum }) {
            $cells = ''
            for ($c = $ColFrom; $c -le $ColTo; $c++) {
                $cells += $rowVals[[string]$c] + ' '
            }
            Write-Output ("{0} row {1:00} cols {2}-{3}: {4}" -f $tag, $rowNum, $ColFrom, $ColTo, $cells)
        }
        $idx++
        $rowVals = @{}
    }
}
Write-Output ("total rows seen for table {0}: {1}" -f $Table, $idx)
