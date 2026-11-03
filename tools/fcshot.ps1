# tools/fcshot.ps1 - screenshot probe over QEMU named pipes (no sockets).
#   Transport : -serial pipe:mangos -> QEMU creates \\.\pipe\mangos.in
#               (host->guest writes) and \\.\pipe\mangos.out (guest->host).
#   Screenshots: guest command 'fcsh' streams the 256x240x4 framebuffer
#               surface over serial (~64 s at 38400 baud).
param(
    [int]$BootWaitSec = 100,
    [int]$TitleWaitSec = 8,
    [int]$StartHoldSec = 6,
    [int]$ScrollSec = 6
)

$ErrorActionPreference = 'Stop'
$root = 'D:\Project\MangoOS'
$img = "$root\image\disk.img"
$qemu = 'D:\Softwares\qemu\qemu-system-i386.exe'
$tmp = "$env:TEMP\opencode"
$ppmPath = "$tmp\shot.ppm"
$pngPath = "$tmp\shot.png"
$oamPath = "$tmp\oam.log"
$errPath = "$tmp\qemu_err.txt"
$pipeName = 'mangos' + (Get-Random -Maximum 999999)

New-Item -ItemType Directory -Force -Path $tmp | Out-Null
if (Test-Path $ppmPath) { Remove-Item $ppmPath -Force }
Get-Process qemu-system-i386 -ErrorAction SilentlyContinue | Stop-Process -Force

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $qemu
$psi.Arguments = @(
    "-hda `"$img`"", '-rtc base=localtime', '-boot', 'c',
    '-serial', "pipe:$pipeName",
    '-audiodev', 'dsound,id=audio0',
    '-machine', 'pcspk-audiodev=audio0',
    '-device', 'sb16,audiodev=audio0',
    '-display', 'none', '-m', '16'
) -join ' '
$psi.UseShellExecute = $false
$psi.CreateNoWindow = $true
$psi.RedirectStandardError = $true
$proc = [System.Diagnostics.Process]::Start($psi)
Write-Output "qemu pid=$($proc.Id)  transport=pipe:$pipeName"

# ---------------- pipe clients ----------------
function Connect-Pipe([string]$name, [System.IO.Pipes.PipeDirection]$dir) {
    $p = New-Object System.IO.Pipes.NamedPipeClientStream('.', $name, $dir)
    $dl = (Get-Date).AddSeconds(20)
    while ($true) {
        try { $p.Connect(500); return $p }
        catch {
            if ((Get-Date) -gt $dl) { throw "pipe $name connect failed" }
            Start-Sleep -Milliseconds 200
        }
    }
}

$outPipe = Connect-Pipe $pipeName InOut        # single duplex pipe
$inPipe  = $outPipe                             # same handle, both ways
Write-Output "pipes connected"

# ---------------- background reader ----------------
$sync = [hashtable]::Synchronized(@{})
$sync.buf = New-Object System.Collections.Generic.List[byte]
$sync.lock = New-Object object
$probeLog = "$tmp\probe.log"
function Log([string]$m) {
    ("{0} {1}" -f (Get-Date -Format 'HH:mm:ss'), $m) |
        Out-File -FilePath $probeLog -Append -Encoding ascii
}
Log "transport ready"

$rs = [runspacefactory]::CreateRunspace()
$rs.Open()
$rs.SessionStateProxy.SetVariable('sync', $sync)
$rs.SessionStateProxy.SetVariable('outPipe', $outPipe)
$ps = [powershell]::Create()
$ps.Runspace = $rs
[void]$ps.AddScript({
    $one = New-Object byte[] 4096
    try {
        while ($true) {
            $n = $outPipe.Read($one, 0, $one.Length)
            if ($n -le 0) { break }
            [System.Threading.Monitor]::Enter($sync.lock)
            try {
                for ($i = 0; $i -lt $n; $i++) { [void]$sync.buf.Add($one[$i]) }
            } finally {
                [System.Threading.Monitor]::Exit($sync.lock)
            }
        }
    } catch {}
})
$ps.BeginInvoke() | Out-Null

$script:log = New-Object System.Text.StringBuilder

function Read-Available {
    $newBytes = $null
    [System.Threading.Monitor]::Enter($sync.lock)
    try {
        if (-not $sync.consumed) { $sync.consumed = 0 }
        if ($sync.consumed -lt $sync.buf.Count) {
            $newBytes = New-Object byte[] ($sync.buf.Count - $sync.consumed)
            for ($i = 0; $i -lt $newBytes.Length; $i++) {
                $newBytes[$i] = $sync.buf[$sync.consumed + $i]
            }
            $sync.consumed += $newBytes.Length
        }
    } finally {
        [System.Threading.Monitor]::Exit($sync.lock)
    }
    if ($newBytes) {
        foreach ($bytc in $newBytes) {
            if ($bytc -ge 32 -or $bytc -eq 13 -or $bytc -eq 10) {
                $script:log.Append([char]$bytc) | Out-Null
            }
        }
    }
}

function Drain-Wait([double]$ms) {
    $dl = (Get-Date).AddMilliseconds($ms)
    $lastBeat = (Get-Date)
    while ((Get-Date) -lt $dl) {
        Read-Available
        if (((Get-Date) - $lastBeat).TotalMilliseconds -ge 500) {
            $cnt = -1
            [System.Threading.Monitor]::Enter($sync.lock)
            try { $cnt = $sync.buf.Count } finally { [System.Threading.Monitor]::Exit($sync.lock) }
            Log ("drain beat cnt=" + $cnt)
            $lastBeat = Get-Date
        }
        Start-Sleep -Milliseconds 50
    }
}

function Send-Cmd([string]$cmd) {
    Log ("send begin '" + $cmd + "'")
    foreach ($ch in ($cmd.ToCharArray())) { $inPipe.WriteByte([byte][char]$ch) }
    $inPipe.WriteByte([byte]13)
    Log "send flushed-bytes done"
    $inPipe.Flush()
    Log "send flush ok"
}

try {
    Write-Output "waiting for shell (up to ${BootWaitSec}s)..."
    $dl = (Get-Date).AddSeconds($BootWaitSec)
    while ((Get-Date) -lt $dl) {
        Read-Available
        if ($proc.HasExited) {
            Write-Output "--- qemu stderr ---"
            try { $proc.StandardError.ReadToEnd() | Write-Output } catch {}
            throw "qemu exited early"
        }
        if ($script:log.ToString() -match 'CMD:') { break }
        Start-Sleep -Milliseconds 200
    }

    Write-Output "entering FC + title wait ${TitleWaitSec}s..."
    Log "send fc"
    Send-Cmd 'fc'; Drain-Wait ($TitleWaitSec * 1000)
    Log ("after fc, loglen=" + $script:log.Length)

    if ($StartHoldSec -gt 0) {
        Write-Output "injecting START..."
        Send-Cmd 'fcbtn 10'; Drain-Wait ($StartHoldSec * 1000)
    }

    if ($ScrollSec -gt 0) {
        Write-Output "holding RIGHT ${ScrollSec}s..."
        Send-Cmd 'fcbtn 01'
        Drain-Wait ($ScrollSec * 1000)
        Send-Cmd 'fcbtn 00'
        Drain-Wait 1500
    }

    Write-Output "state dumps..."
    Log "text dumps"
    Send-Cmd 'fcs';   Drain-Wait 700
    Send-Cmd 'fcv 0'; Drain-Wait 2000
    Send-Cmd 'fcv 1'; Drain-Wait 2000
    Send-Cmd 'fco';   Drain-Wait 2000
    Send-Cmd 'fcp';   Drain-Wait 2000
    Send-Cmd 'fcpw';  Drain-Wait 3000
    Log ("dumps done, loglen=" + $script:log.Length)

    # ---- framebuffer screenshot over serial ----
    Write-Output "capturing framebuffer (~64 s at 38400 baud)..."
    Log "fcsh begin"
    $total = 256 * 240 * 4
    Send-Cmd 'fcsh'
    [System.Threading.Monitor]::Enter($sync.lock)
    try { $start = $sync.buf.Count } finally { [System.Threading.Monitor]::Exit($sync.lock) }
    $dl = (Get-Date).AddSeconds(150)
    while ((Get-Date) -lt $dl) {
        Start-Sleep -Milliseconds 500
        [System.Threading.Monitor]::Enter($sync.lock)
        try { $gotNow = $sync.buf.Count - $start } finally { [System.Threading.Monitor]::Exit($sync.lock) }
        if ($gotNow -ge $total) { break }
        if (($gotNow % 8192) -lt 512) { Log ("fb " + $gotNow + "/" + $total) }
    }
    Log ("fcsh end, got=" + $gotNow)
    Write-Output ("framebuffer bytes=" + $gotNow + "/" + $total)
    $got = [Math]::Min($gotNow, $total)
    if ($got -ge $total) {
        $raw = New-Object byte[] $total
        [System.Threading.Monitor]::Enter($sync.lock)
        try {
            for ($i = 0; $i -lt $total; $i++) { $raw[$i] = $sync.buf[$start + $i] }
            $sync.consumed = $start + $total
        } finally { [System.Threading.Monitor]::Exit($sync.lock) }

        # BGRA little-endian -> P6 ppm (RGB, top-down)
        $w = 256; $h = 240
        $hdr = [Text.Encoding]::ASCII.GetBytes("P6`n$w $h`n255`n")
        $rgb = New-Object byte[] ($w * $h * 3)
        for ($i = 0; $i -lt ($w * $h); $i++) {
            $rgb[$i * 3 + 0] = $raw[$i * 4 + 2]
            $rgb[$i * 3 + 1] = $raw[$i * 4 + 1]
            $rgb[$i * 3 + 2] = $raw[$i * 4 + 0]
        }
        $outBytes = New-Object byte[] ($hdr.Length + $rgb.Length)
        [Array]::Copy($hdr, 0, $outBytes, 0, $hdr.Length)
        [Array]::Copy($rgb, 0, $outBytes, $hdr.Length, $rgb.Length)
        [IO.File]::WriteAllBytes($ppmPath, $outBytes)
        Write-Output "ppm written: $ppmPath"
    } else {
        Write-Output "framebuffer capture incomplete"
    }

    Send-Cmd 'fcbtn 00'
}
catch {
    Write-Output ("ERROR: " + $_.Exception.Message)
}
finally {
    if ($ps) { $ps.Stop() }
    if ($rs) { $rs.Close() }
    if ($inPipe) { $inPipe.Dispose() }
    if ($outPipe) { $outPipe.Dispose() }
    if ($proc -and -not $proc.HasExited) {
        $proc.Kill(); $proc.WaitForExit(5000) | Out-Null
    }
}

[IO.File]::WriteAllText($oamPath, $script:log.ToString())
Write-Output "log saved: $oamPath"

if (Test-Path $ppmPath) {
    & (Join-Path $PSScriptRoot 'ppm2png.ps1') -Ppm $ppmPath -Png $pngPath
}
