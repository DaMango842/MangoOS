# tools/fcprobe.ps1 - sustained horizontal-scrolling stress test.
#   boot -> shell -> fc -> title -> inject START -> hold RIGHT ~90 s,
#   sampling fcdbg every ~15 s; prints frame/pc/MODE per sample and flags
#   a probable freeze when consecutive samples show an unchanged PC.
param(
    [int]$BootWaitSec = 100,
    [int]$TitleWaitSec = 8,
    [int]$StartHoldSec = 6,
    [int]$ScrollSec = 90,
    [int]$SampleEverySec = 15,
    [string]$LogPath = "$env:TEMP\opencode\smb_scroll.log"
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$img = Join-Path $root 'image\disk.img'
$qemu = 'D:\Softwares\qemu\qemu-system-i386.exe'
$tcpPort = 5555

if (-not (Test-Path $img)) { Write-Host "no disk.img"; exit 1 }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $qemu
$psi.Arguments = @(
    "-hda `"$img`"", '-rtc base=localtime', '-boot c',
    "-serial tcp:127.0.0.1:$tcpPort,server,nowait",
    '-display none', '-m 16'
) -join ' '
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Host "qemu pid=$($proc.Id)"

try {
    $c = New-Object System.Net.Sockets.TcpClient
    $dl = (Get-Date).AddSeconds(20)
    while ($true) {
        try { $c.Connect('127.0.0.1', $tcpPort); break }
        catch {
            if ((Get-Date) -gt $dl) { throw "serial connect failed" }
            Start-Sleep -Milliseconds 300
        }
    }
    $s = $c.GetStream()
    $s.ReadTimeout = 100
    $enc = [System.Text.Encoding]::ASCII
    $buf = New-Object byte[] 65536
    $script:log = New-Object System.Text.StringBuilder
    $script:samples = New-Object System.Collections.Generic.List[string]

    function Read-Available {
        try {
            while ($s.DataAvailable) {
                $n = $s.Read($buf, 0, $buf.Length)
                if ($n -le 0) { return }
                [void]$script:log.Append($enc.GetString($buf, 0, $n))
            }
        } catch {}
    }

    function Send-Cmd([string]$cmd) {
        $b = $enc.GetBytes("$cmd`r")
        $s.Write($b, 0, $b.Length)
    }

    # --- boot to shell ---
    Write-Host "waiting for shell (up to ${BootWaitSec}s)..."
    $dl = (Get-Date).AddSeconds($BootWaitSec)
    while ((Get-Date) -lt $dl) {
        Read-Available
        if ($script:log.ToString() -match 'CMD:') { break }
        if ($proc.HasExited) { throw "qemu exited early" }
        Start-Sleep -Milliseconds 200
    }

    # --- enter FC, wait for title ---
    Write-Host "entering FC..."
    Send-Cmd 'fc'
    Start-Sleep -Seconds $TitleWaitSec
    Read-Available

    # --- START to begin the game, then hold RIGHT ---
    Write-Host "injecting START..."
    Send-Cmd 'fcbtn 10'
    Start-Sleep -Seconds $StartHoldSec
    Write-Host "holding RIGHT for ${ScrollSec}s (sampling every ${SampleEverySec}s)..."
    Send-Cmd 'fcbtn 01'

    $deadline = (Get-Date).AddSeconds($ScrollSec)
    $scrollStart = Get-Date
    $nextSample = (Get-Date).AddSeconds($SampleEverySec)
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 1
        Read-Available
        if ((Get-Date) -ge $nextSample) {
            Send-Cmd 'fcdbg'
            Start-Sleep -Milliseconds 800
            Read-Available
            # pull the newest DBG line out of the raw log
            $txt = $script:log.ToString()
            $ms = [regex]::Matches($txt, 'DBG frame=(\d+) pc=([0-9A-F]{4})[^\r\n]*')
            if ($ms.Count -gt 0) {
                $m = $ms[$ms.Count - 1]
                $el = [int]((Get-Date) - $scrollStart).TotalSeconds
                $entry = "t+${el}s frame=$($m.Groups[1].Value) pc=$($m.Groups[2].Value)"
                $script:samples.Add($entry)
                Write-Host ("  sample: {0}" -f $entry)
            }
            $nextSample = (Get-Date).AddSeconds($SampleEverySec)
        }
    }

    # --- final state dump ---
    Send-Cmd 'fcdbg'
    Start-Sleep -Seconds 2
    Read-Available
    Send-Cmd 'fcq'
    Start-Sleep -Seconds 2
    Read-Available

    # --- freeze analysis: consecutive samples with identical pc AND
    #     non-advancing frame counter indicate a stuck CPU ---
    Write-Host "`n=== FREEZE ANALYSIS ==="
    $uniq = @()
    foreach ($line in $script:samples) {
        if ($uniq.Count -eq 0 -or $uniq[-1] -ne $line) { $uniq += $line }
    }
    if ($uniq.Count -lt 2) {
        Write-Host "not enough samples"
    } else {
        $stuck = $true
        for ($i = 1; $i -lt $uniq.Count; $i++) {
            $a = $uniq[$i - 1]; $b = $uniq[$i]
            $fa = if ($a -match 'frame=(\d+)') { [int]$Matches[1] } else { -1 }
            $fb = if ($b -match 'frame=(\d+)') { [int]$Matches[1] } else { -2 }
            $pa = if ($a -match 'pc=([0-9A-F]{4})') { $Matches[1] } else { '?' }
            $pb = if ($b -match 'pc=([0-9A-F]{4})') { $Matches[1] } else { '?' }
            if ($fa -ne $fb -or $pa -ne $pb) { $stuck = $false }
        }
        foreach ($u in $uniq) { Write-Host "  $u" }
        if ($stuck) { Write-Host 'RESULT: LIKELY FROZEN (pc/frame static across samples)' }
        else { Write-Host 'RESULT: alive (frame counter advancing)' }
    }
}
finally {
    if ($s) { $s.Close() }
    if ($c) { $c.Close() }
    if ($proc -and -not $proc.HasExited) {
        $proc.Kill(); $proc.WaitForExit(5000) | Out-Null
    }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $LogPath) | Out-Null
$script:log.ToString() | Out-File -FilePath $LogPath -Encoding ascii
Write-Host "full log: $LogPath"