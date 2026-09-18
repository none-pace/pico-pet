$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Monitor
$results=@();$tcpBefore=0;$dnsBefore=1;$conciseBefore=1;$script:desk=[IntPtr]::Zero
try {
 [void](Message $pet 0x111 240);Start-Sleep -Milliseconds 300
 $script:desk=[MonitorWin]::FindWindow('PicoPet.SystemDesk','PICO 系统 · 本机状态')
 if($script:desk -eq [IntPtr]::Zero){throw 'System panel did not open'}
 Choose 3070 1
 $tcpBefore=Message (Control 3028) 0xF0;$dnsBefore=Message (Control 3027) 0xF0
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,60,50,1420,870,0x14)
 Choose 3024 0
 $conciseBefore=Message (Control 3052) 0xF0
 [void](Message (Control 3052) 0xF1 1);[void](Message $desk 0x111 3052)
 $conciseRows=Rows
 [void](Message (Control 3052) 0xF1 0);[void](Message $desk 0x111 3052)
 $completeRows=Rows
 if($conciseRows -le 0 -or $conciseRows -ge $completeRows){throw 'Concise performance view did not reduce routine rows'}
 Choose 3024 1
 for($i=0;$i -lt 100;$i++){Start-Sleep -Milliseconds 100;if((Message (Control 3025) 0x146) -gt 1){break}}
 $all=Rows;$classes=Message (Control 3025) 0x146
 if($all -le 0 -or $classes -le 1){throw 'Hardware inventory is empty'}
 Capture 'hardware-all'
 if((Columns) -ne 4){throw 'Hardware overview is not compact'}
 ClickFirstRow
 [void](Message (Control 3069) 0xF1 1);[void](Message $desk 0x111 3069)
 $detail=ReadControl 3032
 if($detail -notmatch '驱动 INF' -or $detail -notmatch '硬件 ID'){throw 'Hardware selection did not reveal all detail fields'}
 Capture 'hardware-details-narrow'
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,60,50,1920,980,0x14)
 Capture 'hardware-details-wide'
 [void](Message $desk 0x111 3034)
 [void](Message (Control 3030) 0xF1 1);[void](Message $desk 0x111 3030)
 if((Columns) -ne 11){throw 'Full hardware columns were lost'}
 [void](Message (Control 3030) 0xF1 0);[void](Message $desk 0x111 3030)
 if((Columns) -ne 4){throw 'Compact hardware toggle did not restore columns'}
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,60,50,1420,870,0x14)
 $inventory=ExportTable 'hardware-inventory'
 if(!($inventory | Where-Object {$_.'驱动版本' -ne '--'})){throw 'No driver versions exported'}
 Choose 3025 1;$filtered=Rows
 if($filtered -le 0 -or $filtered -ge $all){throw 'Hardware category did not filter the list'}
 SetFilter 3026 '__pico_no_such_device__';if((Rows) -ne 0){throw 'Hardware text filter failed'}
 SetFilter 3026 '';Choose 3025 0
 $results+=@{devices=$all;categories=$classes-1;classFilterRows=$filtered;driverCsv=$true}
 [MonitorWin]::StartTraffic()
 [void](Message $pet 0x111 242);Start-Sleep -Milliseconds 300
 Choose 3017 0
 [void](Message (Control 3069) 0xF1 1);[void](Message $desk 0x111 3069)
 [void](Message (Control 3028) 0xF1 1);[void](Message $desk 0x111 3028)
 SetFilter 3021 ([string]$PID);Choose 3029 1
 Start-Sleep -Milliseconds 2500
 if((Rows) -lt 2){throw 'Owned local connection is not displayed'}
 if((Columns) -ne 6){throw 'Network overview is not compact'}
 Capture 'network-local-rates'
 ClickFirstRow
 if((ReadControl 3032) -notmatch '本机端口' -or (ReadControl 3032) -notmatch '测速状态'){throw 'Network detail panel omitted source fields'}
 Capture 'network-details'
 [void](Message $desk 0x111 3034)
 [void](Message $desk 0x111 3031)
 Capture 'network-options'
 [void](Message $desk 0x111 3031)
 [void](Message (Control 3030) 0xF1 1);[void](Message $desk 0x111 3030)
 if((Columns) -ne 17){throw 'Full network columns were lost'}
 [void](Message (Control 3030) 0xF1 0);[void](Message $desk 0x111 3030)
 $connections=ExportTable 'network-local'
 if(!($connections | Where-Object {$_.'远端端口' -eq [string][MonitorWin]::Port -and $_.'通信范围' -eq '本机回环' -and $_.'远端 PTR 域名' -eq 'localhost（本机）'})){throw 'Loopback address, domain or destination port incorrect'}
 $results+=@{localConnections=$connections.Count;tcpRateStates=@($connections.'测速状态' | Select-Object -Unique)}
 Choose 3017 3;if((Rows) -le 0){throw 'Process rates view is empty'}
 ClickFirstRow
 Start-Sleep -Milliseconds 2200
 if((ReadControl 3053) -notmatch '进程 TCP 活动' -or (ReadControl 3053) -notmatch '样本 [2-9]'){throw 'Process selection did not switch or advance the activity timeline'}
 Capture 'network-process-rates'
 Choose 3017 2;SetFilter 3021 '';if((Rows) -le 0){throw 'Adapter view is empty'}
 $results+=@{adapters=Rows}
 Choose 3017 1;Choose 3029 0;if((Rows) -le 0){throw 'Expanded event log is empty'}
 Choose 3017 0
 Choose 3029 0;SetFilter 3021 ''
 Start-Sleep -Seconds 3
 $observed=ExportTable 'network-observed'
 $results+=@{ptrNames=@($observed.'远端 PTR 域名' | Where-Object {$_ -match '\.' -and $_ -notmatch '反查'} | Select-Object -Unique);observedConnections=$observed.Count}
 Capture 'network-overview'
 $completeNetworkRows=Rows;[void](Message (Control 3052) 0xF1 1);[void](Message $desk 0x111 3052);$conciseNetworkRows=Rows
 if($conciseNetworkRows -ne $completeNetworkRows){throw 'Concise network view must preserve endpoint evidence'}
 Capture 'network-concise'
 [void](Message (Control 3052) 0xF1 0);[void](Message $desk 0x111 3052)
 [void](Message $desk 0x111 3016)
 SetFilter 3021 '__pico_no_such_endpoint__';if((Rows) -ne 0){throw 'Paused filter did not update'}
 SetFilter 3021 '';[void](Message $desk 0x111 3016)
 [void][MonitorWin]::ShowWindow($desk,6)
 $process=Get-Process PicoPet;$cpu=$process.TotalProcessorTime.TotalSeconds
 Start-Sleep -Seconds 3;$process=Get-Process -Id $process.Id
 $results+=@{minimizedCpuSeconds=$process.TotalProcessorTime.TotalSeconds-$cpu;workingSetMB=[Math]::Round($process.WorkingSet64/1MB,2)}
 [void][MonitorWin]::ShowWindow($desk,9)
 $process=Get-Process PicoPet;$cpu=$process.TotalProcessorTime.TotalSeconds
 Start-Sleep -Seconds 5;$process=Get-Process -Id $process.Id
 $results+=@{networkCpuPercentOneCore=[Math]::Round(20*($process.TotalProcessorTime.TotalSeconds-$cpu),3);networkWorkingSetMB=[Math]::Round($process.WorkingSet64/1MB,2)}
 [void](Message $pet 0x111 241);Start-Sleep -Milliseconds 500
 DiskTab 0;Capture 'disk-browse-compact'
 if((ReadControl 3053) -notmatch '磁盘活动'){throw 'Disk view did not expose an activity timeline'}
 DiskTab 1;Capture 'disk-snapshots-compact'
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,60,50,1240,850,0x14)
 Capture 'disk-snapshots-small'
 $listRect=New-Object MonitorWin+RECT;$filterRect=New-Object MonitorWin+RECT
 [void][MonitorWin]::GetWindowRect((Control 3019),[ref]$listRect);[void][MonitorWin]::GetWindowRect((Control 3009),[ref]$filterRect)
 if($filterRect.Bottom -gt $listRect.Top){throw 'Snapshot controls overlap the list'}
 DiskTab 0
 [void](Message $pet 0x111 240);Choose 3024 0;Capture 'performance-compact'
 $results+=@{conciseRows=$conciseRows;completeRows=$completeRows;conciseNetworkRows=$conciseNetworkRows;completeNetworkRows=$completeNetworkRows;activityTimelines=$true;compactAndFullViews=$true;responsiveDetails=$true;diskLayouts=$true}
 $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'output/monitor-test.json') -Encoding utf8
 $results | ConvertTo-Json -Depth 5
}finally{
 if($desk -ne [IntPtr]::Zero){[void][MonitorWin]::PostMessage((Control 3019),0x202,[IntPtr]::Zero,[IntPtr]::Zero)}
 $dialog=[MonitorWin]::FindWindow('#32770','另存为');if($dialog -ne [IntPtr]::Zero){[void][MonitorWin]::PostMessage($dialog,0x111,[IntPtr]2,[IntPtr]::Zero)}
 [MonitorWin]::StopTraffic()
 if($desk -ne [IntPtr]::Zero){
  [void](Message (Control 3028) 0xF1 $tcpBefore);[void](Message (Control 3027) 0xF1 $dnsBefore);[void](Message $desk 0x111 3028)
  SetFilter 3021 '';Choose 3029 0
  [void](Message (Control 3052) 0xF1 $conciseBefore);[void](Message $desk 0x111 3052)
 }
 [void][MonitorWin]::SetThreadDpiAwarenessContext($oldDpi)
}
