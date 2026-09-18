param([switch]$HD)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class HoverWin {
 [StructLayout(LayoutKind.Sequential)]public struct POINT{public int X,Y;}
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out POINT p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr d);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr d,int x,int y,int w,int h,IntPtr s,int sx,int sy,uint op);
}
'@
$hwnd=[HoverWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($hwnd -eq [IntPtr]::Zero){throw 'Start PICO first.'}
$oldDpi=[HoverWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$cursor=New-Object HoverWin+POINT
[void][HoverWin]::GetCursorPos([ref]$cursor)
function Send([uint32]$message,[int]$argument=0){[HoverWin]::SendMessage($hwnd,$message,[IntPtr]$argument,[IntPtr]::Zero).ToInt64()}
function Rect {$r=New-Object HoverWin+RECT;[void][HoverWin]::GetWindowRect($hwnd,[ref]$r);return $r}
function MoveCursor([int]$x,[int]$y){[void][HoverWin]::SetCursorPos($x,$y);[void](Send 0x200)}
$wasHD=Send 0x8003 15
[void](Send 0x111 $(if($HD){261}else{260}))
$manifest=Get-Content (Join-Path $root $(if($HD){'assets/hd-manifest.json'}else{'assets/manifest.json'})) -Raw | ConvertFrom-Json
$wasFloating=((Send 0x8003 4) -band 1) -ne 0
$checks=0
try {
 if($wasFloating){[void](Send 0x111 108)}
 foreach($view in @(210,211)){
  foreach($size in @(200,201,202)){
   [void](Send 0x111 $view);[void](Send 0x111 $size);[void](Send 0x111 222)
   $r=Rect;$width=$r.Right-$r.Left
   $patch=$manifest.views[(Send 0x8003 5)].patch
   $x=$r.Left+[int](($patch[0]+$patch[2]/2)*$width/$manifest.sourceSize)
   $y=$r.Top+[int](($patch[1]+$patch[3]/2)*$width/$manifest.sourceSize)
   MoveCursor $x $y
   $hoverFace=Send 0x8003 9
   if((Send 0x8003 14) -ne 1){
    $hit=$false
    for($sourceY=$patch[1];$sourceY -lt $patch[1]+$patch[3] -and !$hit;$sourceY+=2){
     for($sourceX=$patch[0];$sourceX -lt $patch[0]+$patch[2] -and !$hit;$sourceX+=2){
      $x=$r.Left+[int](($sourceX+.5)*$width/$manifest.sourceSize);$y=$r.Top+[int](($sourceY+.5)*$width/$manifest.sourceSize)
      MoveCursor $x $y
      if((Send 0x8003 14) -eq 1){$hit=$true;$hoverFace=Send 0x8003 9}
     }
    }
   }
   $hovered=Send 0x8003 14
   if($hovered -ne 1 -or $hoverFace -lt 8 -or $hoverFace -gt 11){throw "Hover failed at view $view size $size (hover=$hovered face=$hoverFace pose=$(Send 0x8003 5))"}
   $checks++
   $wakes=Send 0x8003 1
   Start-Sleep -Milliseconds 250
   if((Send 0x8003 1) -ne $wakes){throw 'Hover introduced continuous timer wakes'}
   $checks++
   if($view -eq 211 -and $size -eq 201){
    $bitmap=New-Object Drawing.Bitmap $width,$width
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try {
     $dc=$graphics.GetHdc();$screen=[HoverWin]::GetDC([IntPtr]::Zero)
     try{[void][HoverWin]::BitBlt($dc,0,0,$width,$width,$screen,$r.Left,$r.Top,0x40CC0020)}
     finally{[void][HoverWin]::ReleaseDC([IntPtr]::Zero,$screen);$graphics.ReleaseHdc($dc)}
     $bitmap.Save((Join-Path $root 'output/computer-hover.png'),[Drawing.Imaging.ImageFormat]::Png)
    }finally{$graphics.Dispose();$bitmap.Dispose()}
   }
   MoveCursor ($r.Left+[int]($width*.82)) ($r.Top+[int]($width*.6))
   if((Send 0x8003 14) -ne 0 -or (Send 0x8003 9) -ne 2){throw 'Moving onto the casing did not restore the prior expression'}
   $checks++
   MoveCursor $x $y
   [void](Send 0x201)
   if((Send 0x8003 14) -ne 0){throw 'Desktop remained active during capture'}
   [void](Send 0x202)
   $desk=[HoverWin]::FindWindow('PicoPet.SystemDesk','PICO 系统 · 本机状态')
   if($desk -eq [IntPtr]::Zero){throw 'Screen icon did not open the system panel'}
   [void][HoverWin]::SendMessage($desk,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
   $checks++
  }
 }
 [void](Send 0x111 210);[void](Send 0x111 202);[void](Send 0x111 220)
 $r=Rect;$width=$r.Right-$r.Left;$found=@{}
 for($y=$r.Top+[int]($width*.35);$y -lt $r.Top+[int]($width*.72) -and $found.Count -lt 4;$y+=2){
  for($x=$r.Left+[int]($width*.18);$x -lt $r.Left+[int]($width*.72) -and $found.Count -lt 4;$x+=2){
   MoveCursor $x $y;$selected=Send 0x8003 16
   if($selected -ge 0 -and $selected -le 3 -and !$found.ContainsKey([int]$selected)){$found[[int]$selected]=@($x,$y)}
  }
 }
 if($found.Count -ne 4){throw "Only found $($found.Count) of four screen buttons"}
 foreach($index in 0..3){
  MoveCursor $found[$index][0] $found[$index][1]
  if((Send 0x8003 16) -ne $index -or (Send 0x8003 9) -ne (8+$index)){throw "Button $index did not show its selected frame"}
  [void](Send 0x201)
  if((Send 0x8003 9) -ne (8+$index)){throw "Button $index did not retain selection while pressed"}
  [void](Send 0x202);Start-Sleep -Milliseconds 100
  $desk=[HoverWin]::FindWindow('PicoPet.SystemDesk','PICO 系统 · 本机状态')
  if($desk -eq [IntPtr]::Zero){throw "Button $index did not open the system panel"}
  $tabs=[HoverWin]::GetDlgItem($desk,3000)
  if([HoverWin]::SendMessage($tabs,0x130B,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64() -ne $index){throw "Button $index opened the wrong system page"}
  [void][HoverWin]::SendMessage($desk,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
  $checks+=2
 }
 MoveCursor 0 0
 [void](Send 0x2A3)
 if((Send 0x8003 14) -ne 0){throw 'Leaving the window did not clear hover'}
 $checks++
} finally {
 [void](Send 0x202)
 [void](Send 0x111 201);[void](Send 0x111 211);[void](Send 0x111 220)
 if($wasFloating){[void](Send 0x111 108)}
 [void](Send 0x111 106)
 [void](Send 0x111 $(if($wasHD){261}else{260}))
 [void][HoverWin]::SetCursorPos($cursor.X,$cursor.Y)
 [void][HoverWin]::SetThreadDpiAwarenessContext($oldDpi)
}
$result=@{checks=$checks;hoverDesktop=$true;screenButtons=4;selectedFrames=$true;pageMapping=$true;restoresExpression=$true;hoverTimerWakes=0}
$result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/hover-test.json') -Encoding utf8
$result | ConvertTo-Json
