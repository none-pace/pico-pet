$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class TopmostCheck {
 [DllImport("user32.dll",EntryPoint="GetWindowLongPtrW")]public static extern IntPtr Style(IntPtr h,int n);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")]public static extern IntPtr GetForegroundWindow();
}
'@
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'PICO is not running'}
function IsTopmost {([TopmostCheck]::Style($pet,-20).ToInt64() -band 8) -ne 0}
if(!(IsTopmost)){throw 'This test expects the keep-on-top preference enabled'}
$foreground=[TopmostCheck]::GetForegroundWindow()
[void][TopmostCheck]::SetWindowPos($pet,[IntPtr](-2),0,0,0,0,0x13)
if(!(IsTopmost)){throw 'Lost topmost flag was not restored'}
if([TopmostCheck]::GetForegroundWindow() -ne $foreground){throw 'Restoring topmost stole input focus'}
[void](Send $pet 0x111 102)
try {
 if(IsTopmost){throw 'Turning off keep-on-top was ignored'}
 [void][TopmostCheck]::SetWindowPos($pet,[IntPtr](-1),0,0,0,0,0x13)
 if(IsTopmost){throw 'Disabled keep-on-top preference was not respected'}
} finally {[void](Send $pet 0x111 102)}
Write-Host 'PASS: topmost recovery, disabled preference and input focus.'
