$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DeskWin {
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern bool SetWindowText(IntPtr h,string text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,System.Text.StringBuilder text,int length);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int s,uint flags);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr d);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr d,int x,int y,int w,int h,IntPtr s,int sx,int sy,uint op);
}
'@
$oldDpi=[DeskWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[DeskWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 Start-Sleep -Milliseconds 500
 $pet=[DeskWin]::FindWindow('PicoPet.Win11.Native','PICO')
}
if($pet -eq [IntPtr]::Zero){throw 'Start PICO first.'}
$results=@()
try {
 foreach($page in @(0,1,2)){
  [void][DeskWin]::SendMessage($pet,0x111,[IntPtr](240+$page),[IntPtr]::Zero)
  Start-Sleep -Milliseconds 1600
  $desk=[DeskWin]::FindWindow('PicoPet.SystemDesk','PICO 系统 · 本机状态')
  if($desk -eq [IntPtr]::Zero -or ![DeskWin]::IsWindowVisible($desk)){throw 'System panel is not visible'}
  $list=[DeskWin]::GetDlgItem($desk,3019)
  $rows=[DeskWin]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
  if($rows -le 0){throw "Page $page has no data rows"}
  $rect=New-Object DeskWin+RECT;[void][DeskWin]::GetWindowRect($desk,[ref]$rect)
  $bitmap=New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
  $graphics=[Drawing.Graphics]::FromImage($bitmap)
  try {
   $dc=$graphics.GetHdc();$screen=[DeskWin]::GetDC([IntPtr]::Zero)
   try{[void][DeskWin]::BitBlt($dc,0,0,$bitmap.Width,$bitmap.Height,$screen,$rect.Left,$rect.Top,0x40CC0020)}
   finally{[void][DeskWin]::ReleaseDC([IntPtr]::Zero,$screen);$graphics.ReleaseHdc($dc)}
   $bitmap.Save((Join-Path $root "output/system-page-$page.png"),[Drawing.Imaging.ImageFormat]::Png)
  }finally{$graphics.Dispose();$bitmap.Dispose()}
  $results+=@{page=$page;rows=$rows;visible=$true;width=$rect.Right-$rect.Left;height=$rect.Bottom-$rect.Top}
 }
 $networkView=[DeskWin]::GetDlgItem($desk,3017)
 [void][DeskWin]::SendMessage($networkView,0x14E,[IntPtr]2,[IntPtr]::Zero)
 [void][DeskWin]::SendMessage($desk,0x111,[IntPtr](3017 -bor (1 -shl 16)),[IntPtr]::Zero)
 $adapterRows=[DeskWin]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
 if($adapterRows -le 0){throw 'No adapter counters displayed'}
 $results+=@{networkAdapters=$adapterRows}
 [void][DeskWin]::SendMessage($networkView,0x14E,[IntPtr]1,[IntPtr]::Zero)
 [void][DeskWin]::SendMessage($desk,0x111,[IntPtr](3017 -bor (1 -shl 16)),[IntPtr]::Zero)
 $logRows=[DeskWin]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
 if($logRows -le 0){throw 'No connection events recorded'}
 $results+=@{networkEvents=$logRows}
 [void][DeskWin]::SendMessage($pet,0x111,[IntPtr]241,[IntPtr]::Zero)
 Start-Sleep -Milliseconds 200
 $snapshots=[DeskWin]::GetDlgItem($desk,3010)
 $prior=[DeskWin]::SendMessage($snapshots,0x146,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
 $folder=Join-Path $root 'src'
 [void][DeskWin]::SendText([DeskWin]::GetDlgItem($desk,3001),0xC,[IntPtr]::Zero,$folder)
 [void][DeskWin]::SendMessage($desk,0x111,[IntPtr]3006,[IntPtr]::Zero)
 for($i=0;$i -lt 50;$i++){
  Start-Sleep -Milliseconds 100
  $count=[DeskWin]::SendMessage($snapshots,0x146,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()
  if($count -gt $prior){break}
 }
 if($count -le $prior){$message=New-Object Text.StringBuilder 2048;[void][DeskWin]::GetWindowText([DeskWin]::GetDlgItem($desk,3020),$message,2048);throw "Snapshot UI did not finish: $message"}
 $timeline=New-Object Text.StringBuilder 2048;[void][DeskWin]::GetWindowText([DeskWin]::GetDlgItem($desk,3053),$timeline,2048)
 if($timeline.ToString() -notmatch '索引活动'){throw 'Snapshot scan did not switch the activity timeline'}
 $results+=@{snapshotCreated=$true;snapshotRows=[DeskWin]::SendMessage($list,0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt64()}
 [void][DeskWin]::SendMessage($pet,0x111,[IntPtr]240,[IntPtr]::Zero)
 $process=Get-Process PicoPet | Select-Object -First 1
 $cpu=$process.TotalProcessorTime.TotalSeconds
 Start-Sleep -Seconds 5
 $process=Get-Process -Id $process.Id
 $results+=@{performancePanelCpuPercentOneCore=20*($process.TotalProcessorTime.TotalSeconds-$cpu);workingSetMB=$process.WorkingSet64/1MB}
 [void][DeskWin]::ShowWindow($desk,6)
 $cpu=$process.TotalProcessorTime.TotalSeconds
 Start-Sleep -Seconds 3
 $process=Get-Process -Id $process.Id
 $results+=@{minimizedCpuSeconds=$process.TotalProcessorTime.TotalSeconds-$cpu}
 [void][DeskWin]::ShowWindow($desk,9)
}finally{[void][DeskWin]::SetThreadDpiAwarenessContext($oldDpi)}
$results | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/system-window-test.json') -Encoding utf8
$results | ConvertTo-Json
