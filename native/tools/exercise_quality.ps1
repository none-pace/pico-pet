$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class QualityWin {
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr d);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr d,int x,int y,int w,int h,IntPtr s,int sx,int sy,uint op);
}
'@
function FindPet {
 for($i=0;$i -lt 50;$i++){
  $script:hwnd=[QualityWin]::FindWindow('PicoPet.Win11.Native','PICO')
  if($script:hwnd -ne [IntPtr]::Zero){return}
  Start-Sleep -Milliseconds 100
 }
 throw 'PICO did not start'
}
function Send([uint32]$m,[int]$v=0){[QualityWin]::SendMessage($script:hwnd,$m,[IntPtr]$v,[IntPtr]::Zero).ToInt64()}
function RestartPet {
 $running=Get-Process PicoPet
 [void](Send 0x111 107)
 $running.WaitForExit()
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 FindPet
}
FindPet
$oldDpi=[QualityWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$previous=Send 0x8003 15
$floating=((Send 0x8003 4) -band 1) -ne 0
$report=@()
try {
 if($floating){[void](Send 0x111 108)}
 [void](Send 0x111 250);[void](Send 0x111 211);[void](Send 0x111 222)
 foreach($quality in @(0,1)){
  [void](Send 0x111 (260+$quality))
  RestartPet
  if((Send 0x8003 15) -ne $quality){throw 'Quality was not restored after restarting'}
  $initial=Get-Process PicoPet
  $cpu=$initial.TotalProcessorTime.TotalSeconds
  $wakes=Send 0x8003 1
  Start-Sleep -Seconds 5
  $after=Get-Process PicoPet
  $label=if($quality){'hd'}else{'pixel'}
  $report+=@{quality=$label;workingSetMB=[Math]::Round($after.WorkingSet64/1MB,2);privateMB=[Math]::Round($after.PrivateMemorySize64/1MB,2);cpuSeconds=$after.TotalProcessorTime.TotalSeconds-$cpu;timerWakes=(Send 0x8003 1)-$wakes;restartPersisted=$true}
  $r=New-Object QualityWin+RECT
  [void][QualityWin]::GetWindowRect($hwnd,[ref]$r)
  $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top)
  $graphics=[Drawing.Graphics]::FromImage($bitmap)
  try {
   $dc=$graphics.GetHdc();$screen=[QualityWin]::GetDC([IntPtr]::Zero)
   try{[void][QualityWin]::BitBlt($dc,0,0,$bitmap.Width,$bitmap.Height,$screen,$r.Left,$r.Top,0x40CC0020)}
   finally{[void][QualityWin]::ReleaseDC([IntPtr]::Zero,$screen);$graphics.ReleaseHdc($dc)}
   $bitmap.Save((Join-Path $root "output/quality-$label.png"),[Drawing.Imaging.ImageFormat]::Png)
  }finally{$graphics.Dispose();$bitmap.Dispose()}
 }
 for($i=0;$i -lt 12;$i++){[void](Send 0x111 260);[void](Send 0x111 261)}
 if((Send 0x8003 15) -ne 1){throw 'Repeated quality switching failed'}
 $report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/quality-test.json') -Encoding utf8
 $report | ConvertTo-Json
}finally{
 [void](Send 0x111 (260+$previous));[void](Send 0x111 220)
 if($floating){[void](Send 0x111 108)}
 [void][QualityWin]::SetThreadDpiAwarenessContext($oldDpi)
}
