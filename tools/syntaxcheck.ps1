# tools/syntaxcheck.ps1
$t = [IO.File]::ReadAllText('D:\Project\MangoOS\tools\fcshot.ps1')
$errs = $null
[void][System.Management.Automation.Language.Parser]::ParseInput($t, [ref]$null, [ref]$errs)
if ($errs -and $errs.Count) {
    foreach ($e in $errs) { Write-Output ("PARSE: " + $e.Message + " @line " + $e.Extent.StartLineNumber) }
} else {
    Write-Output "SYNTAX OK"
}
# show first bytes for BOM check
$b = [IO.File]::ReadAllBytes('D:\Project\MangoOS\tools\fcshot.ps1')
$s = ''
for ($i = 0; $i -lt 8; $i++) { $s += '{0:X2} ' -f $b[$i] }
Write-Output ("head8: " + $s)
# show the invocation-relevant line endings count of CRLF vs LF
$cr = ($t.ToCharArray() | Where-Object { $_ -eq "`r" }).Count
Write-Output ("CR count=" + $cr)