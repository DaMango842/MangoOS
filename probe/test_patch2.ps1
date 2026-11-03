function Log([string]$msg) { [System.Console]::WriteLine($msg) }

function Read-Bytes([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "missing" }
    Log("  -> read " + $path)
    [System.IO.File]::ReadAllBytes($path)
}

function Set-Word([byte[]]$dst, [int]$off, [uint16]$val) {
    $b = [System.BitConverter]::GetBytes($val)
    [System.Array]::Copy($b, 0, $dst, $off, 2)
}

$a = Read-Bytes 'D:\Project\MangoOS\image\file_table.bin'
Write-Output ("type: " + $a.GetType().FullName)
$v0 = [System.BitConverter]::ToUInt16($a, 10)
Set-Word $a 10 ([uint16]7)
$v1 = [System.BitConverter]::ToUInt16($a, 10)
Write-Output ("before @10: {0}  after @10: {1}" -f $v0, $v1)