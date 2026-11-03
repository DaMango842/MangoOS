# Build the 16 MB MangoOS disk image.
#
# build_img.ps1 IS the single layout planner: no LBA / sector-count is
# hardcoded anywhere else. On every build it:
#   1. computes each blob's position by sequential allocation
#   2. writes the BootMap (LBA 1) used by stage1 to locate stage2 AND the
#      raw kernel blob (stage1 hands both to stage2 via 0x520-0x52B)
#   3. records the kernel-FS region start (everything after the kernel is
#      owned by the kernel's own MangoFS v2)
#
# Resulting dynamic layout:
#   LBA 0      : stage1 boot sector
#   LBA 1      : BootMap ('MBOOT', ver 2: stage2 lba/sec, fs_base,
#                         kernel lba/sec)
#   LBA 2..n   : stage2
#   then       : KERNEL.BIN (RAW blob, no file system involved)
#   then       : kernel FS region (kernel formats it on first boot)
$ErrorActionPreference = 'Stop'

$root      = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$imageDir  = Join-Path $root "image"
$img    = Join-Path $imageDir "disk.img"
$s1     = Join-Path $imageDir "stage1.bin"
$s2     = Join-Path $imageDir "stage2.bin"
$krn    = Join-Path $imageDir "kernel.bin"

function Log([string]$msg) { [System.Console]::WriteLine($msg) }

function Read-Bytes([string]$path, [string]$what) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required build product missing: $path. Run 'make' first."
    }
    Log "  -> $what to read"
    return ,([System.IO.File]::ReadAllBytes($path))
}

function Write-Sector([System.IO.FileStream]$fs, [int]$lba, [byte[]]$bytes) {
    $fs.Seek($lba * 512, [System.IO.SeekOrigin]::Begin) | Out-Null
    $fs.Write($bytes, 0, $bytes.Length)
    $rem = (512 - ($bytes.Length % 512)) % 512
    if ($rem -gt 0) {
        $pad = New-Object byte[] $rem
        $fs.Write($pad, 0, $rem)
    }
}

function Set-DWord([byte[]]$dst, [int]$off, [uint32]$val) {
    $b = [System.BitConverter]::GetBytes($val)
    [System.Array]::Copy($b, 0, $dst, $off, 4)
}

# ---------------- read build products ----------------
$bStage1 = Read-Bytes $s1 "stage1.bin"
$bStage2 = Read-Bytes $s2 "stage2.bin"
$bKernel = Read-Bytes $krn "kernel.bin"

Log "  -> kernel.bin   $($bKernel.Length) B"

# ---------------- layout planning (dynamic, always) ----------------
$stage2_lba = 2                       # LBA 0 = stage1, LBA 1 = BootMap
$stage2_sec = [int][math]::Ceiling($bStage2.Length / 512.0)

$kernel_lba = $stage2_lba + $stage2_sec
$kernel_sec = [int][math]::Ceiling($bKernel.Length / 512.0)
$fs_base    = $kernel_lba + $kernel_sec
$fs_sec     = 32768 - $fs_base        # kernel FS owns the rest of the disk

# ---------------- BootMap v2 (LBA 1) ----------------
# 'MBOOT' | ver=2 | stage2_lba(4) | stage2_sec(4) | fs_base(4)
#         | kernel_lba(4) | kernel_sec(4)
$bm = New-Object byte[] 512
[System.Text.Encoding]::ASCII.GetBytes('MBOOT').CopyTo($bm, 0)
$bm[5] = 0x02                          # BootMap version 2 (byte 4 = 'T')
$bm[6] = 0x01                          # stage2 loader version
Set-DWord $bm 7  $stage2_lba
Set-DWord $bm 11 $stage2_sec
Set-DWord $bm 15 ([uint32]$fs_base)
Set-DWord $bm 19 ([uint32]$kernel_lba)
Set-DWord $bm 23 ([uint32]$kernel_sec)

# ---------------- write the image ----------------
Log ""
Log "Layout:"
Log ("  stage1    @ LBA 0")
Log ("  BootMap   @ LBA 1 (ver 2)")
Log ("  stage2    @ LBA {0} ({1} sectors)" -f $stage2_lba, $stage2_sec)
Log ("  KERNEL.BIN @ LBA {0} ({1} sectors) [raw]" -f $kernel_lba, $kernel_sec)
Log ("  kernel FS @ LBA {0} ({1} sectors)" -f $fs_base, $fs_sec)

$fs = [System.IO.FileStream]::new($img, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write)
try {
    $fs.SetLength(16 * 1024 * 1024)

    # zero-fill first (important: the FS region must start unformatted)
    $zero = New-Object byte[] (16 * 1024 * 1024)
    $fs.Write($zero, 0, $zero.Length)
    $fs.Flush()

    Write-Sector $fs 0           $bStage1
    Write-Sector $fs 1           $bm
    Write-Sector $fs $stage2_lba $bStage2
    Write-Sector $fs $kernel_lba $bKernel

    $fs.Flush()
}
finally {
    $fs.Dispose()
}

Log ""
Log "Disk image built: $img"