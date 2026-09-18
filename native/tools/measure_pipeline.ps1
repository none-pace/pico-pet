param([string]$Name='pipeline',[ValidateSet('rotate','drag','float','idle')][string]$Mode='rotate')
$ErrorActionPreference='Stop'
Add-Type @'
using System;using System.Diagnostics;using System.Runtime.InteropServices;using System.Threading;
public static class PipelineProbe {
 [StructLayout(LayoutKind.Sequential)]public struct Rect{public int L,T,R,B;}
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int X,Y;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string n);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")]static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")]static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("winmm.dll")]static extern uint timeBeginPeriod(uint p);
 [DllImport("winmm.dll")]static extern uint timeEndPeriod(uint p);
 public static long Send(IntPtr h,uint m,int w=0){return SendMessage(h,m,(IntPtr)w,IntPtr.Zero).ToInt64();}
 public static double[] Run(IntPtr h,string mode){
  Point cursor;GetCursorPos(out cursor);Rect r;GetWindowRect(h,out r);
  bool floating=(Send(h,0x8003,4)&1)!=0,changed=false,held=false;
  int x=r.L+(r.R-r.L)/2,y=r.T+(r.B-r.T)*35/100;
  uint pid;GetWindowThreadProcessId(h,out pid);var process=Process.GetProcessById((int)pid);
  timeBeginPeriod(1);
  try{
   if((mode=="float")!=floating){Send(h,0x111,108);changed=true;}
   if(mode=="rotate"||mode=="drag"){SetCursorPos(x,y);Send(h,mode=="rotate"?0x207u:0x201u);held=true;}
   Send(h,0x8003,49);int[] ids={0,36,47,50,51,52,53};var before=new long[ids.Length];
   for(int i=0;i<ids.Length;i++)before[i]=Send(h,0x8003,ids[i]);
   var cpu=process.TotalProcessorTime;var clock=Stopwatch.StartNew();
   while(clock.Elapsed.TotalSeconds<4){
    if(held){double t=clock.Elapsed.TotalSeconds;SetCursorPos(x+(int)(100*Math.Sin(t*2)),y+(int)(20*Math.Sin(t)));Send(h,0x200);}
    Thread.Sleep(4);
   }
   double seconds=clock.Elapsed.TotalSeconds,cpuMs=(process.TotalProcessorTime-cpu).TotalMilliseconds;
   var result=new double[12];
   for(int i=0;i<ids.Length;i++)result[i]=Send(h,0x8003,ids[i])-before[i];
   result[7]=Send(h,0x8003,54)/1000.0;result[8]=Send(h,0x8003,55)/1000.0;
   result[9]=seconds;result[10]=cpuMs;result[11]=r.R-r.L;return result;
  }finally{
   if(held){SetCursorPos(x,y);Send(h,0x200);Send(h,mode=="rotate"?0x208u:0x202u);}
   if(changed)Send(h,0x111,108);SetCursorPos(cursor.X,cursor.Y);timeEndPeriod(1);
  }
 }
}
'@
[void][PipelineProbe]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[PipelineProbe]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'PICO is not running'}
if(([PipelineProbe]::Send($pet,0x8003,2) -band 5) -ne 0){throw 'The pet must be visible and unpaused'}
$result=[PipelineProbe]::Run($pet,$Mode)
$frames=[Math]::Max(1,$result[0])
$report=[ordered]@{mode=$Mode;extent=$result[11];targetFps=[PipelineProbe]::Send($pet,0x8003,17);asyncReadback=[PipelineProbe]::Send($pet,0x8003,57);frames=$result[0];fps=$result[0]/$result[9];meanRenderMs=$result[1]/1000/$frames;pickTriangleTests=$result[2];meanSubmitMs=$result[3]/1000/$frames;meanReadbackMs=$result[4]/1000/$frames;meanCopyMs=$result[5]/1000/$frames;meanPresentMs=$result[6]/1000/$frames;p95FrameGapMs=$result[7];maxFrameGapMs=$result[8];cpuMs=$result[10];seconds=$result[9]}
$report|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot "../output/$Name.json")
$report|ConvertTo-Json
if($Mode -in @('rotate','drag') -and $result[0] -lt 80){throw 'Too few frames during continuous interaction'}
