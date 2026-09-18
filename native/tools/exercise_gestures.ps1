$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class GestureWin {
 [StructLayout(LayoutKind.Sequential)]public struct POINT{public int X,Y;}
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out POINT p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int s,uint flags);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
$hwnd=[GestureWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($hwnd -eq [IntPtr]::Zero){throw 'Start PICO first.'}
$oldDpi=[GestureWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$original=New-Object GestureWin+POINT
[void][GestureWin]::GetCursorPos([ref]$original)
function Send([uint32]$message,[int]$argument=0){[GestureWin]::SendMessage($hwnd,$message,[IntPtr]$argument,[IntPtr]::Zero).ToInt64()}
function Rect {$r=New-Object GestureWin+RECT;[void][GestureWin]::GetWindowRect($hwnd,[ref]$r);return $r}
$results=@()
$proc=Get-Process PicoPet | Select-Object -First 1
$cpuBefore=$proc.TotalProcessorTime.TotalSeconds
$wakesBefore=Send 0x8003 1
$drawsBefore=Send 0x8003 0
$compositionsBefore=Send 0x8003 11
$mapsBefore=Send 0x8003 12
$decodesBefore=Send 0x8003 13
$watch=[Diagnostics.Stopwatch]::StartNew()
try {
 if(((Send 0x8003 4) -band 1) -eq 0){[void](Send 0x111 108)}
 foreach($name in @('slow','flick','reverse','upward','hold','shake','circle','top-grip','bottom-grip')){
  [void](Send 0x111 211)
  [void](Send 0x111 106)
  $r=Rect
  $left=[Math]::Max(200,$r.Left-600);$top=[Math]::Max(300,$r.Top-400)
  [void][GestureWin]::SetWindowPos($hwnd,[IntPtr]::Zero,$left,$top,0,0,0x15)
  $r=Rect;$sx=[int](($r.Left+$r.Right)/2);$sy=[int](($r.Top+$r.Bottom)/2)
  if($name -eq 'top-grip'){$sy-=55}
  if($name -eq 'bottom-grip'){$sy+=55}
  [void][GestureWin]::SetCursorPos($sx,$sy)
  [void](Send 0x201)
  $poses=@();$rolls=@();$shaking=$false;$lastX=$sx;$lastY=$sy
  $steps=if($name -in @('shake','circle')){48}else{20}
  for($i=1;$i -le $steps;$i++){
   $dx=0;$dy=0
   switch($name){
    'slow' {$dx=$i}
    'reverse' {$dx=if($i -le 10){$i*14}else{140-($i-10)*16}}
    'upward' {$dy=-$i*12}
    'shake' {$dx=[Math]::Sin($i*[Math]::PI/3)*80}
    'circle' {$dx=75*([Math]::Cos($i*[Math]::PI/12)-1);$dy=75*[Math]::Sin($i*[Math]::PI/12)}
    default {$dx=$i*12}
   }
   $lastX=$sx+[int]$dx;$lastY=$sy+[int]$dy
   [void][GestureWin]::SetCursorPos($lastX,$lastY)
   [void](Send 0x200)
   Start-Sleep -Milliseconds 16
   $poses+=Send 0x8003 5;$rolls+=Send 0x8003 6
   $shaking=$shaking -or ((Send 0x8003 8) -ne 0)
  }
  if($name -eq 'hold'){
   Start-Sleep -Milliseconds 2000
   $heldWakes=Send 0x8003 1
   Start-Sleep -Milliseconds 400
   $heldAfter=Send 0x8003 1
   if($heldAfter -ne $heldWakes){throw "A stationary held pet kept waking timers: delta=$($heldAfter-$heldWakes), state=$(Send 0x8003 2), angular=$(Send 0x8003 10)"}
  }
  $releaseSpeed=Send 0x8003 7
  $angularSpeed=Send 0x8003 10
  [void](Send 0x202)
  $release=Rect
  Start-Sleep -Milliseconds 300
  $after=Rect
  $results+=@{name=$name;dx=$after.Left-$release.Left;dy=$after.Top-$release.Top;speed=$releaseSpeed;angularSpeed=$angularSpeed;shaking=$shaking;poseCount=@($poses | Select-Object -Unique).Count;minRoll=($rolls | Measure-Object -Minimum).Minimum;maxRoll=($rolls | Measure-Object -Maximum).Maximum}
 }
} finally {
 [void](Send 0x202)
 [void](Send 0x111 211)
 [void](Send 0x111 106)
 [void][GestureWin]::SetCursorPos($original.X,$original.Y)
 [void][GestureWin]::SetThreadDpiAwarenessContext($oldDpi)
}
$watch.Stop()
$proc=Get-Process -Id $proc.Id
$report=@{scenarios=$results;durationSeconds=$watch.Elapsed.TotalSeconds;cpuPercentOneCore=100*($proc.TotalProcessorTime.TotalSeconds-$cpuBefore)/$watch.Elapsed.TotalSeconds;workingSetMB=$proc.WorkingSet64/1MB;timerWakes=(Send 0x8003 1)-$wakesBefore;redraws=(Send 0x8003 0)-$drawsBefore;compositions=(Send 0x8003 11)-$compositionsBefore;mappingBuilds=(Send 0x8003 12)-$mapsBefore;poseDecodes=(Send 0x8003 13)-$decodesBefore;heldStillStopsTimers=$true}
$report | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'output/gesture-test.json') -Encoding utf8
$report | ConvertTo-Json -Depth 5
foreach($r in $results){
 if($r.name -in @('slow','hold') -and ([Math]::Abs($r.dx) -gt 2 -or [Math]::Abs($r.dy) -gt 2)){throw "$($r.name): unexpected stale throw"}
 if($r.name -eq 'flick' -and $r.dx -le 3){throw 'Flick did not continue right'}
 if($r.name -eq 'reverse' -and $r.dx -ge -3){throw 'Reversal released in the wrong direction'}
 if($r.name -eq 'upward' -and $r.dy -ge -3){throw 'Upward toss did not continue up'}
 if($r.name -eq 'shake' -and !$r.shaking){throw 'Repeated shake was not detected'}
 if($r.name -in @('slow','flick','circle') -and $r.shaking){throw "$($r.name): false shake"}
 if($r.name -eq 'top-grip' -and $r.maxRoll -le 0){throw 'Top grip torque not visible'}
 if($r.name -eq 'bottom-grip' -and $r.minRoll -ge 0){throw 'Bottom grip torque not visible'}
}
