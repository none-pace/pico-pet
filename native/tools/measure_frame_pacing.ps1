param([string]$Name='frame-pacing')
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
Add-Type @'
using System;using System.Diagnostics;using System.Collections.Generic;using System.Runtime.InteropServices;using System.Threading;
public static class FramePacing {
 [DllImport("user32.dll")]static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("winmm.dll")]static extern uint timeBeginPeriod(uint p);
 [DllImport("winmm.dll")]static extern uint timeEndPeriod(uint p);
 static long Send(IntPtr h,uint m,int w=0){return SendMessage(h,m,(IntPtr)w,IntPtr.Zero).ToInt64();}
 public static double[] Run(IntPtr h,int x,int y){
  timeBeginPeriod(1);var gaps=new List<double>();var clock=Stopwatch.StartNew();
  long first=Send(h,0x8003,0),previous=first,micros=Send(h,0x8003,36);double last=0;
  try{
   SetCursorPos(x,y);Send(h,0x207);
   while(clock.Elapsed.TotalSeconds<4){double t=clock.Elapsed.TotalSeconds;
    SetCursorPos(x+(int)(140*Math.Sin(t*2)),y+(int)(20*Math.Sin(t)));Send(h,0x200);
    long frame=Send(h,0x8003,0);double now=clock.Elapsed.TotalMilliseconds;
    if(frame>previous){if(last>0)gaps.Add((now-last)/(frame-previous));previous=frame;last=now;}
    Thread.Sleep(4);
   }
  }finally{Send(h,0x208);timeEndPeriod(1);}
  gaps.Sort();double seconds=clock.Elapsed.TotalSeconds;long frames=Send(h,0x8003,0)-first;
  return new double[]{frames/seconds,(Send(h,0x8003,36)-micros)/1000.0/Math.Max(1,frames),gaps.Count==0?0:gaps[(int)((gaps.Count-1)*.95)],frames,seconds};
 }
}
'@
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'PICO is not running'}
if(((Send $pet 0x8003 2) -band 5) -ne 0){throw 'Frame measurement requires a visible, unpaused pet'}
[void](Send $pet 0x111 210)
$r=New-Object EmbeddedWin+RECT
[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
$cursor=New-Object EmbeddedWin+POINT
[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
try{$result=[FramePacing]::Run($pet,($r.Left+($r.Right-$r.Left)/2),($r.Top+($r.Bottom-$r.Top)*.35))}finally{[void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y)}
$report=@{fps=$result[0];meanRenderMs=$result[1];observedP95FrameGapMs=$result[2];frames=$result[3];seconds=$result[4];extent=$r.Right-$r.Left}
$report|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot "../output/$Name.json")
$report|ConvertTo-Json
if($result[3] -lt 100){throw 'Too few frames during continuous rotation'}
