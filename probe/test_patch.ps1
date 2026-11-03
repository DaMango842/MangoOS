function Set-Word([byte[]]$dst, [int]$off, [uint16]$val) {
    $b = [System.BitConverter]::GetBytes($val)
    [System.Array]::Copy($b, 0, $dst, $off, 2)
}

$a = [System.IO.File]::ReadAllBytes('D:\Project\MangoOS\image\file_table.bin')
$v0 = [System.BitConverter]::ToUInt16($a, 10)
Set-Word $a 10 ([uint16]6)
$v1 = [System.BitConverter]::ToUInt16($a, 10)
Write-Output ("before patch @10: {0}  after patch @10: {1}" -f $v0, $v1)
[System.IO.File]::WriteAllBytes('D:\Project\MangoOS\probe\test_patch.bin', $a)