param([ValidateSet('Exit','Float','Pause','Show','Reset','Pixel','HD','DeskSize','Console')][string]$Action = 'Exit')
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PicoControl {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string cls,string title);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll")]public static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
}
'@
$handle=[PicoControl]::FindWindow('PicoPet.Win11.Native','PICO')
if($handle -ne [IntPtr]::Zero){
 [uint32]$petProcessId=0;[void][PicoControl]::GetWindowThreadProcessId($handle,[ref]$petProcessId)
 $process=Get-Process -Id $petProcessId -ErrorAction SilentlyContinue
 $ids=@{Exit=107;Float=108;Pause=101;Show=100;Reset=106;Pixel=260;HD=261;DeskSize=250;Console=244}
 [void][PicoControl]::PostMessage($handle,0x111,[IntPtr]$ids[$Action],[IntPtr]::Zero)
 if($Action -eq 'Exit' -and $process -and !$process.WaitForExit(10000)){throw 'PICO did not exit within 10 seconds'}
}
