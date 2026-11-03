param()
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class Win32 {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
$script:found = [IntPtr]::Zero
$cb = [Win32+EnumProc]{
  param($h, $l)
  $sb = New-Object System.Text.StringBuilder 256
  [Win32]::GetWindowText($h, $sb, 256) | Out-Null
  if ($sb.ToString() -match "QEMU" -and [Win32]::IsWindowVisible($h)) {
    $script:found = $h
    return $false
  }
  return $true
}
[Win32]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:found -eq [IntPtr]::Zero) { Write-Output "NO_WINDOW"; exit 1 }
$h = $script:found
$cr = New-Object Win32+RECT
[Win32]::GetClientRect($h, [ref]$cr) | Out-Null
$pt = New-Object Win32+POINT
[Win32]::ClientToScreen($h, [ref]$pt) | Out-Null
Write-Output ("CLIENT_W {0} CLIENT_H {1} SCREEN_X {2} SCREEN_Y {3}" -f $cr.R, $cr.B, $pt.X, $pt.Y)
$b = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($b.Location, [System.Drawing.Point]::Empty, $b.Size)
$bmp.Save("D:\Project\MangoOS\probe\full.png", [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose()
$bmp.Dispose()
Write-Output "SAVED"