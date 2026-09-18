$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Monitor
$root=Split-Path $PSScriptRoot -Parent
$results=[ordered]@{}
$petWasHidden=((Message $pet 0x8003 2) -band 1) -ne 0
function AwaitRows {for($i=0;$i -lt 80;$i++){if((Rows) -gt 0){return};Start-Sleep -Milliseconds 100};throw 'No rows after background sampling'}
function ClickRow { $scale=[MonitorWin]::GetDpiForWindow((Control 3019))/96;$point=([int](40*$scale) -shl 16) -bor [int](50*$scale);[void][MonitorWin]::PostMessage((Control 3019),0x201,[IntPtr]1,[IntPtr]$point);[void][MonitorWin]::PostMessage((Control 3019),0x202,[IntPtr]::Zero,[IntPtr]$point);Start-Sleep -Milliseconds 200 }
try {
 if(!$petWasHidden){[void](Message $pet 0x111 100)}
 [void](Message $pet 0x111 242);Start-Sleep -Milliseconds 400
 $script:desk=[MonitorWin]::FindWindow('PicoPet.SystemDesk','PICO 系统 · 本机状态')
 if($desk -eq [IntPtr]::Zero){throw 'System panel did not open'}
 Choose 3070 1
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,30,30,1560,1000,0x14)
 [void](Message $desk 0x111 3067);Choose 3017 4
 [MonitorWin]::StartTraffic();$port=[MonitorWin]::Port
 SetFilter 3021 ([string]$PID);AwaitRows
 if((Columns) -ne 5){throw 'Software overview should have five summary columns'}
 ClickRow
 [void](Message (Control 3069) 0xF1 0);[void](Message $desk 0x111 3069)
 $detail=ReadControl 3032
 if($detail.Length -gt 300 -or $detail -notmatch 'TCP'){throw 'Software interpretation should briefly describe measured traffic'}
 Capture 'usability-software-overview'
 [void](Message $desk 0x111 3066)
 if((Message (Control 3017) 0x147) -ne 5 -or (ReadControl 3022) -notmatch '正在查看'){throw 'Software focus did not open retained connection history'}
 SetFilter 3021 ([string]$port);AwaitRows
 ClickRow
 [void](Message (Control 3069) 0xF1 1);[void](Message $desk 0x111 3069)
 $firstDetail=ReadControl 3032
 if($firstDetail -notmatch '首次可见' -or $firstDetail -notmatch '当前可见'){throw 'History lacks observation timestamps and live status'}
 $listRect=New-Object MonitorWin+RECT;$detailRect=New-Object MonitorWin+RECT
 [void][MonitorWin]::GetWindowRect((Control 3019),[ref]$listRect);[void][MonitorWin]::GetWindowRect((Control 3032),[ref]$detailRect)
 if($detailRect.Left -le $listRect.Right -or $detailRect.Top -gt $listRect.Top+120){throw 'Detail pane moved under the list'}
 [void](Message $desk 0x111 3065);$frozenRows=Rows
 [MonitorWin]::StopTraffic();Start-Sleep -Milliseconds 3500
 if((Rows) -ne $frozenRows -or (ReadControl 3032) -ne $firstDetail){throw 'Frozen reading changed under the user'}
 if((ReadControl 3020) -notmatch '[1-9]\d* 次更新待查看'){throw 'Freezing the view stopped background collection'}
 [void](Message $desk 0x111 3065);Start-Sleep -Milliseconds 200
 $history=ExportTable 'usability-history'
 if(!($history|Where-Object {$_.'观测状态' -eq '已结束 / 不再可见' -and ($_.'本机端口' -eq [string]$port -or $_.'远端端口' -eq [string]$port)})){throw 'Closed local endpoint disappeared from retained history'}
 if((ReadControl 3032) -notmatch '观测状态'){throw 'Selection did not survive history refresh'}
 [void](Message (Control 3069) 0xF1 1);[void](Message $desk 0x111 3069)
 if((ReadControl 3032) -notmatch '进程启动标识' -or (ReadControl 3032) -notmatch '本机端口'){throw 'Full details lost raw identity or port fields'}
 [void](Message (Control 3069) 0xF1 0);[void](Message $desk 0x111 3069)
 Capture 'usability-software-history'
 Choose 3017 1
 $log=ExportTable 'usability-software-log'
 if(!($log|Where-Object {$_.'事件' -eq '消失'})){throw 'Focused software log lost disconnect events'}
 if(@($log.'程序路径' | Select-Object -Unique).Count -gt 1){throw 'Focused software log contains unrelated programs'}
 $results.softwareFocus=$true;$results.historyRetention=$true;$results.freezeKeepsSampling=$true;$results.rightDetailPane=$true
 [MonitorWin]::StartTraffic();$secondPort=[MonitorWin]::Port
 Choose 3017 0;SetFilter 3021 ([string]$secondPort);AwaitRows;ClickRow
 [void](Message (Control 3069) 0xF1 1);[void](Message $desk 0x111 3069)
 $liveDetails=ReadControl 3032
 [MonitorWin]::StopTraffic();Start-Sleep -Milliseconds 3200
 if((Rows) -eq 0 -or (ReadControl 3032) -notmatch [regex]::Escape([string]$secondPort)){throw 'Recently ended connection cleared the detail being read'}
 $results.vanishedSelectionRetained=$true
 [void](Message $desk 0x111 3067);SetFilter 3021 '';[void](Message $pet 0x111 240);Choose 3024 2;SetFilter 3026 'PicoPet.exe';AwaitRows;Start-Sleep -Milliseconds 1400
 ClickRow;$processDetail=ReadControl 3032
 foreach($field in @('CPU','内存工作集','私有提交','线程数','句柄数','启动时间','程序路径')){if($processDetail -notmatch $field){throw "Process detail missing $field"}}
 if((Columns) -ne 6){throw 'Process resource overview should have six columns'}
 Choose 3024 1
 if((ReadControl 3026) -ne ''){throw 'Process filter leaked into hardware inventory'}
 Choose 3024 2;AwaitRows;ClickRow
 if((ReadControl 3026) -ne 'PicoPet.exe'){throw 'Process filter was not restored'}
 Capture 'usability-processes'
 [void](Message $desk 0x111 3065);$before=ReadControl 3032;Start-Sleep -Milliseconds 2300
 if((ReadControl 3032) -ne $before){throw 'Process reading does not stay frozen'}
 [void](Message $desk 0x111 3065)
 $results.processResources=$true
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,30,30,1280,940,0x14)
 $listRect=New-Object MonitorWin+RECT;$detailRect=New-Object MonitorWin+RECT
 [void][MonitorWin]::GetWindowRect((Control 3019),[ref]$listRect);[void][MonitorWin]::GetWindowRect((Control 3032),[ref]$detailRect)
 if($detailRect.Left -le $listRect.Right){throw 'Narrow-window detail pane moved below the list'}
 Capture 'usability-narrow-processes'
 [void](Message $desk 0x111 3066)
 if((ReadControl 3022) -notmatch 'PicoPet.exe'){throw 'Process-to-network navigation lost executable identity'}
 [void](Message $desk 0x111 3067);Choose 3017 4;SetFilter 3021 ''
 Capture 'usability-all-software'
 $process=Get-Process PicoPet|Select-Object -First 1;$cpu=$process.TotalProcessorTime.TotalMilliseconds
 Start-Sleep -Milliseconds 3100;$process.Refresh();$results.networkCpuMsPer3Seconds=$process.TotalProcessorTime.TotalMilliseconds-$cpu
 [void][MonitorWin]::ShowWindow($desk,6);Start-Sleep -Milliseconds 500;$process.Refresh();$cpu=$process.TotalProcessorTime.TotalMilliseconds
 Start-Sleep -Milliseconds 2100;$process.Refresh();$results.minimizedCpuMsPer2Seconds=$process.TotalProcessorTime.TotalMilliseconds-$cpu
 [void][MonitorWin]::ShowWindow($desk,9)
 $results|ConvertTo-Json|Set-Content (Join-Path $root 'output/usability-checks.json')
 $results|ConvertTo-Json
} finally {
 [MonitorWin]::StopTraffic()
 if($script:desk -ne [IntPtr]::Zero){[void](Message $desk 0x10)}
 if(!$petWasHidden){[void](Message $pet 0x111 100)}
 [void][MonitorWin]::SetThreadDpiAwarenessContext($oldDpi)
}
