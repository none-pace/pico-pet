$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Monitor
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ReadabilityCheck {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr FindWindow(string c,string t);
 public static IntPtr Desk(){return FindWindow("PicoPet.SystemDesk",null);}
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
}
'@
function Await([scriptblock]$condition,[string]$failure){for($i=0;$i -lt 100;$i++){if(& $condition){return};Start-Sleep -Milliseconds 100};throw $failure}
function FirstCard {[MonitorWin]::GetDlgItem((Control 3071),32000)}
function ChooseSort([int]$mode){for($i=0;$i -lt (Message (Control 3076) 0x146);$i++){if((Message (Control 3076) 0x150 $i) -eq $mode){Choose 3076 $i;return}};throw "Missing sort mode $mode"}
function SelectCard {[void](Message (FirstCard) 0xF5);Start-Sleep -Milliseconds 100}
function ReadWindow([IntPtr]$h){$s=New-Object Text.StringBuilder 4096;[void][MonitorWin]::ReadText($h,0xD,[IntPtr]4096,$s);$s.ToString()}
$fixture=$null
try {
 [void](Message $pet 0x111 240);Await { [ReadabilityCheck]::Desk() -ne [IntPtr]::Zero } 'System desk missing'
 $script:desk=[ReadabilityCheck]::Desk()
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,20,20,1560,980,0x14)
 Choose 3070 0;Choose 3024 2;Choose 3079 0
 $fixture=Start-Process -FilePath (Join-Path $env:SystemRoot 'System32/ping.exe') -ArgumentList '-t','127.0.0.1' -WindowStyle Hidden -PassThru
 SetFilter 3026 'ping.exe'
 Await { (Rows) -eq 1 } 'Fixture process not sampled'
 ChooseSort 5;SelectCard
 if((ReadControl 3032).Length -gt 300){throw 'Interpretation is still too long'}
 for($i=0;$i -lt 5;$i++){Start-Sleep -Milliseconds 650;if([ReadabilityCheck]::IsWindowVisible((Control 3019))){throw 'Hidden table reappeared over cards during refresh'}}
 $boardRect=New-Object MonitorWin+RECT;$sortRect=New-Object MonitorWin+RECT
 [void][MonitorWin]::GetWindowRect((Control 3071),[ref]$boardRect);[void][MonitorWin]::GetWindowRect((Control 3076),[ref]$sortRect)
 if($sortRect.Bottom -gt $boardRect.Top){throw 'Sort control overlaps cards'}
 $fixture.Kill();$fixture.WaitForExit();$fixture=$null
 Await { (Rows) -eq 1 -and (ReadControl 3032).Contains('60') } 'Exited process was removed instead of retained'
 if((ReadControl 3032) -notmatch '--'){throw 'Ended process still has live counters'}
 Capture 'cards-retained-process'
 Choose 3079 1;Await { (Rows) -eq 0 } 'Current-only filter includes exited process'
 Choose 3079 2;Await { (Rows) -eq 1 } 'Recently missing filter lost exited process'
 Choose 3079 0;SetFilter 3026 '';ChooseSort 3
 [void](Message (Control 3077) 0xF1 1);[void](Message $desk 0x111 3077)
 Start-Sleep -Milliseconds 1800
 if([ReadabilityCheck]::IsWindowVisible((Control 3019))){throw 'Live sorting reveals backing table'}
 [void](Message (Control 3077) 0xF1 0);[void](Message $desk 0x111 3077)
 [void](Message (Control 3071) 0x20A (-120 -shl 16));Start-Sleep -Milliseconds 150;Capture 'cards-scroll-clean'
 [MonitorWin]::StartTraffic();[void](Message $pet 0x111 242);Choose 3017 0
 SetFilter 3021 ([string][MonitorWin]::Port)
 Await { (Rows) -ge 2 } 'Loopback connections not sampled';SelectCard
 $before=Rows;[MonitorWin]::StopTraffic()
 Start-Sleep -Milliseconds 3000
 if((Rows) -lt $before){throw 'Connections disappeared too soon'}
 if([ReadabilityCheck]::IsWindowVisible((Control 3019))){throw 'Network refresh reveals backing table'}
 Capture 'cards-retained-connections'
 [void](Message $desk 0x111 3065);$frozen=ReadControl 3032;Start-Sleep -Milliseconds 1500
 if((ReadControl 3032) -ne $frozen){throw 'Frozen interpretation changed'}
 [void](Message $desk 0x111 3065)
 @{shortInterpretation=$true;hiddenTableStaysHidden=$true;sortToolbar=$true;latestStart=$true;recentProcesses=$true;recentConnections=$true;readingFreeze=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/readability-checks.json')
 Get-Content (Join-Path $root 'output/readability-checks.json')
} finally {
 if($fixture -and !$fixture.HasExited){$fixture.Kill();$fixture.WaitForExit()}
 [MonitorWin]::StopTraffic()
 if($script:desk -ne [IntPtr]::Zero){[void](Message $desk 0x10)}
 [void][MonitorWin]::SetThreadDpiAwarenessContext($oldDpi)
}
