$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Monitor
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class CardCheck {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr FindWindow(string c,string t);
 public static IntPtr Desk(){return FindWindow("PicoPet.SystemDesk",null);}
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern IntPtr SetFocus(IntPtr h);
 [DllImport("user32.dll")]public static extern uint GetGuiResources(IntPtr p,uint flags);
}
'@
function Await([scriptblock]$condition,[string]$failure){for($i=0;$i -lt 100;$i++){if(& $condition){return};Start-Sleep -Milliseconds 100};throw $failure}
function FirstCard {[MonitorWin]::GetDlgItem((Control 3071),32000)}
function ClickCard {[void](Message (FirstCard) 0xF5);Start-Sleep -Milliseconds 150}
$results=[ordered]@{}
try {
 [MonitorWin]::StartTraffic()
 [void](Message $pet 0x111 242);Await { [CardCheck]::Desk() -ne [IntPtr]::Zero } 'System desk missing'
 $script:desk=[CardCheck]::Desk()
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,20,20,1560,980,0x14)
 Choose 3070 0;Choose 3017 4;Choose 3072 0
 SetFilter 3021 '';Await { (Rows) -gt 1 } 'Network overview missing'
 Start-Sleep -Milliseconds 3200;Capture 'cards-network-overview'
 SetFilter 3021 ([string]$PID)
 Await { (Rows) -eq 1 } 'Software grouping did not isolate the fixture'
 if(![CardCheck]::IsWindowVisible((Control 3071)) -or [CardCheck]::IsWindowVisible((Control 3019))){throw 'Default presentation is not cards'}
 ClickCard
 if((ReadControl 3032) -notmatch 'TCP'){throw 'Card selection did not reveal measured coverage'}
 $results.cardSelection=$true
 Capture 'cards-software-detail'
 Choose 3072 3;Await { (Rows) -eq 1 } 'Local filter hid loopback fixture'
 Choose 3072 2
 if((Rows) -ne 0){throw 'Public filter included loopback fixture'}
 if(![CardCheck]::IsWindowVisible((Control 3039))){throw 'Empty results must be visible'}
 Choose 3072 0;$results.scopeFiltering=$true
 Choose 3070 2
 if((Columns) -lt 18 -or ![CardCheck]::IsWindowVisible((Control 3019))){throw 'Full table lost evidence fields'}
 Choose 3070 0;ClickCard
 [void](Message $desk 0x111 3065);$detail=ReadControl 3032;Start-Sleep -Milliseconds 2100
 if((ReadControl 3032) -ne $detail){throw 'Frozen card evidence changed during reading'}
 [void](Message $desk 0x111 3065);$results.fullFieldsAndFreeze=$true
 [void](Message $desk 0x111 3066)
 if((Message (Control 3017) 0x147) -ne 5){throw 'Software investigation did not reach retained endpoints'}
 Capture 'cards-endpoint-evidence'
 [void](Message $pet 0x111 240);Choose 3024 2;SetFilter 3026 ''
 Await { (Rows) -gt 1 } 'Process view empty'
 $grouped=Rows
 [void](Message (Control 3074) 0xF1 0);[void](Message $desk 0x111 3074);$individual=Rows
 if($individual -lt $grouped){throw 'Process grouping increased record count'}
 [void](Message (Control 3074) 0xF1 1);[void](Message $desk 0x111 3074)
 $results.groupedProcesses=$grouped;$results.individualProcesses=$individual
 Start-Sleep -Milliseconds 2200
 $cpuSort=-1;for($i=0;$i -lt (Message (Control 3076) 0x146);$i++){if((Message (Control 3076) 0x150 $i) -eq 3){$cpuSort=$i;break}}
 if($cpuSort -lt 0){throw 'CPU sorting option missing'}
 Choose 3076 $cpuSort;Capture 'cards-process-overview'
 ClickCard
 [void](Message (FirstCard) 0x100 0x27)
 $selected=[MonitorWin]::SendMessage((Control 3019),0x100C,[IntPtr](-1),[IntPtr]2).ToInt32()
 if($selected -ne 1){throw 'Arrow-key card navigation did not select the next record'}
 $results.keyboardNavigation=$true
 SetFilter 3026 'PicoPet.exe';Await { (Rows) -eq 1 } 'Own process missing';ClickCard
 Capture 'cards-process-detail'
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,20,20,1280,940,0x14)
 Capture 'cards-compact'
 $boardRect=New-Object MonitorWin+RECT;$detailRect=New-Object MonitorWin+RECT
 [void][MonitorWin]::GetWindowRect((Control 3071),[ref]$boardRect);[void][MonitorWin]::GetWindowRect((Control 3032),[ref]$detailRect)
 if($boardRect.Right -ge $detailRect.Left){throw 'Cards overlap evidence panel'}
 [void](Message $pet 0x111 241);Await { (Rows) -gt 0 } 'Disk cards missing';Capture 'cards-disks'
 [void](Message $pet 0x111 243);Choose 3040 3
 [void](Message (Control 3052) 0xF1 0);[void](Message $desk 0x111 3052)
 Await { (Rows) -gt 0 } 'Security cards missing';Capture 'cards-security'
 $process=Get-Process PicoPet | Select-Object -First 1
 $gdiBefore=[CardCheck]::GetGuiResources($process.Handle,0)
 for($i=0;$i -lt 20;$i++){Choose 3070 1;Choose 3070 0}
 $gdiAfter=[CardCheck]::GetGuiResources($process.Handle,0)
 if($gdiAfter-$gdiBefore -gt 8){throw 'Card/table switching leaks GDI resources'}
 $results.gdiGrowth=$gdiAfter-$gdiBefore
 [void](Message $pet 0x111 242);[void](Message $desk 0x111 3067);SetFilter 3021 '';Start-Sleep -Milliseconds 800
 $process.Refresh();$cpu=$process.TotalProcessorTime.TotalMilliseconds
 Start-Sleep -Milliseconds 3100;$process.Refresh();$results.networkCpuMsPer3Seconds=$process.TotalProcessorTime.TotalMilliseconds-$cpu
 [void][MonitorWin]::ShowWindow($desk,6);Start-Sleep -Milliseconds 500;$process.Refresh();$cpu=$process.TotalProcessorTime.TotalMilliseconds
 Start-Sleep -Milliseconds 2100;$process.Refresh();$results.minimizedCpuMs=$process.TotalProcessorTime.TotalMilliseconds-$cpu
 $results.layout=$true;$results.allModules=$true
 $results|ConvertTo-Json|Set-Content (Join-Path $root 'output/cards-checks.json')
 $results|ConvertTo-Json
} finally {
 [MonitorWin]::StopTraffic()
 if($script:desk -ne [IntPtr]::Zero){[void](Message $desk 0x10)}
 [void][MonitorWin]::SetThreadDpiAwarenessContext($oldDpi)
}
