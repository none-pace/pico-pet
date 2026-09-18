$ErrorActionPreference = 'Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DragWin {
 [StructLayout(LayoutKind.Sequential)]public struct POINT{public int X,Y;}
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out POINT p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr d);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr d,int x,int y,int w,int h,IntPtr s,int sx,int sy,uint op);
 [DllImport("dwmapi.dll")]public static extern int DwmGetWindowAttribute(IntPtr h,int a,out int v,int size);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr hwnd);
}
'@
$hwnd=[DragWin]::FindWindow('PicoPet.Win11.Native','PICO')
$previousDpi=[DragWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
if($hwnd -eq [IntPtr]::Zero){throw 'Start PICO first.'}
function Send([uint32]$message,[int]$argument=0){[DragWin]::SendMessage($hwnd,$message,[IntPtr]$argument,[IntPtr]::Zero).ToInt64()}
if(((Send 0x8003 4) -band 1) -eq 0){[void](Send 0x111 108)}
[void](Send 0x111 201)
[void](Send 0x111 106)
[void](Send 0x111 211)
$original=New-Object DragWin+POINT
[void][DragWin]::GetCursorPos([ref]$original)
$rect=New-Object DragWin+RECT
[void][DragWin]::GetWindowRect($hwnd,[ref]$rect)
$centerX=[int](($rect.Left+$rect.Right)/2)
$centerY=[int](($rect.Top+$rect.Bottom)/2)
try {
 $initialPose=Send 0x8003 5
 $postureChanged=$false
 [void][DragWin]::SetCursorPos($centerX,$centerY)
 [void](Send 0x201)
 for($i=1;$i -le 12;$i++){
  [void][DragWin]::SetCursorPos(($centerX-$i*18),($centerY-$i*8))
  [void](Send 0x200)
  Start-Sleep -Milliseconds 16
  $postureChanged=$postureChanged -or ((Send 0x8003 5) -ne $initialPose) -or ((Send 0x8003 6) -ne 0)
 }
 $dragPose=Send 0x8003 5
 $dragRoll=Send 0x8003 6
 [void](Send 0x202)
 $released=New-Object DragWin+RECT
 [void][DragWin]::GetWindowRect($hwnd,[ref]$released)
 Start-Sleep -Milliseconds 350
 $coasting=New-Object DragWin+RECT
 [void][DragWin]::GetWindowRect($hwnd,[ref]$coasting)
 $continued=($coasting.Left -lt $released.Left-3) -and ($coasting.Top -lt $released.Top-3)
 if(!$continued){throw 'The pet did not continue moving after release.'}
} finally {
 [void](Send 0x202)
 [void][DragWin]::SetCursorPos($original.X,$original.Y)
}
Start-Sleep -Milliseconds 30
[void][DragWin]::GetWindowRect($hwnd,[ref]$rect)
$bitmap=New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
$graphics=[Drawing.Graphics]::FromImage($bitmap)
try {
 $destination=$graphics.GetHdc();$source=[DragWin]::GetDC([IntPtr]::Zero)
 try{[void][DragWin]::BitBlt($destination,0,0,$bitmap.Width,$bitmap.Height,$source,$rect.Left,$rect.Top,0x40CC0020)}
 finally{[void][DragWin]::ReleaseDC([IntPtr]::Zero,$source);$graphics.ReleaseHdc($destination)}
 $bitmap.Save((Join-Path $root 'output/drag-preview.png'),[Drawing.Imaging.ImageFormat]::Png)
}finally{$graphics.Dispose();$bitmap.Dispose()}
$cloaked=0
[void][DragWin]::DwmGetWindowAttribute($hwnd,14,[ref]$cloaked,4)
[void](Send 0x111 101)
$result=@{throwContinued=$continued;released=@($released.Left,$released.Top);coasting=@($coasting.Left,$coasting.Top);dwmCloaked=$cloaked;dpi=[DragWin]::GetDpiForWindow($hwnd);size=@(($rect.Right-$rect.Left),($rect.Bottom-$rect.Top));initialPose=$initialPose;dragPose=$dragPose;dragRoll=$dragRoll}
[void](Send 0x111 101)
[void](Send 0x111 108)
$fixed=New-Object DragWin+RECT
[void][DragWin]::GetWindowRect($hwnd,[ref]$fixed)
$rx=[int](($fixed.Left+$fixed.Right)/2);$ry=[int](($fixed.Top+$fixed.Bottom)/2)
try{
 [void][DragWin]::SetCursorPos($rx,$ry);[void](Send 0x207)
 for($i=1;$i -le 6;$i++){
  [void][DragWin]::SetCursorPos(($rx+$i*18),($ry-$i*3));[void](Send 0x200);Start-Sleep -Milliseconds 20
 }
 [void](Send 0x208);Start-Sleep -Milliseconds 500
 $rotated=Send 0x8003 5
 $afterRotation=New-Object DragWin+RECT
 [void][DragWin]::GetWindowRect($hwnd,[ref]$afterRotation)
 $result.middleRotatesWithoutMoving=($rotated -ne $initialPose -and $fixed.Left -eq $afterRotation.Left -and $fixed.Top -eq $afterRotation.Top)
 [void](Send 0x111 201)
 $beforeZoom=New-Object DragWin+RECT
 [void][DragWin]::GetWindowRect($hwnd,[ref]$beforeZoom)
 [void](Send 0x20A (120 -shl 16));Start-Sleep -Milliseconds 100
 $afterZoom=New-Object DragWin+RECT
 [void][DragWin]::GetWindowRect($hwnd,[ref]$afterZoom)
 $result.wheelEnlarges=($afterZoom.Right-$afterZoom.Left) -gt ($beforeZoom.Right-$beforeZoom.Left)
 $result.wheelPreservesAngle=(Send 0x8003 5) -eq $rotated
 [void](Send 0x20A (-120 -shl 16));Start-Sleep -Milliseconds 100
 [void][DragWin]::GetWindowRect($hwnd,[ref]$afterZoom)
 $result.wheelShrinks=($afterZoom.Right-$afterZoom.Left) -eq ($beforeZoom.Right-$beforeZoom.Left)
}finally{[void](Send 0x208);[void][DragWin]::SetCursorPos($original.X,$original.Y)}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/drag-test.json') -Encoding utf8
$result | ConvertTo-Json
[void](Send 0x111 108)
[void](Send 0x111 211)
[void](Send 0x111 106)
if(!$postureChanged){throw 'Dragging did not change the model posture.'}
if(!$result.middleRotatesWithoutMoving -or !$result.wheelEnlarges -or !$result.wheelShrinks -or !$result.wheelPreservesAngle){throw 'Rotation or wheel zoom controls failed.'}
[void][DragWin]::SetThreadDpiAwarenessContext($previousDpi)
