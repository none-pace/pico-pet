param([switch]$Codex)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DiagnosticsWin {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 [DllImport("user32.dll")]public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,System.Text.StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,System.Text.StringBuilder l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int s,uint flags);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr d);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr d,int x,int y,int w,int h,IntPtr s,int sx,int sy,uint op);
 public static IntPtr FindFilename(IntPtr dialog){
  IntPtr result=IntPtr.Zero;
  EnumChildWindows(dialog,(h,p)=>{var cls=new System.Text.StringBuilder(256);GetClassName(h,cls,256);if(cls.ToString()=="Edit" && GetDlgCtrlID(h)==1001){result=h;return false;}return true;},IntPtr.Zero);
  return result;
 }
}
'@
function Message([IntPtr]$h,[uint32]$m,[int]$v=0){[DiagnosticsWin]::SendMessage($h,$m,[IntPtr]$v,[IntPtr]::Zero).ToInt64()}
function Control([int]$id){[DiagnosticsWin]::GetDlgItem($script:desk,$id)}
function Choose([int]$id,[int]$item){[void](Message (Control $id) 0x14E $item);[void](Message $script:desk 0x111 ($id -bor (1 -shl 16)))}
function SetText([int]$id,[string]$value){[void][DiagnosticsWin]::SendText((Control $id),0xC,[IntPtr]::Zero,$value)}
function ReadControl([int]$id){$text=New-Object Text.StringBuilder 262144;[void][DiagnosticsWin]::ReadText((Control $id),0xD,[IntPtr]262144,$text);$text.ToString()}
function Rows {Message (Control 3019) 0x1004}
function Columns {$header=[DiagnosticsWin]::SendMessage((Control 3019),0x101F,[IntPtr]::Zero,[IntPtr]::Zero);Message $header 0x1200}
function AwaitRead {
 for($i=0;$i -lt 200;$i++){Start-Sleep -Milliseconds 100;if(![DiagnosticsWin]::IsWindowEnabled((Control 3051))){return}}
 throw 'Diagnostics query did not complete within 20 seconds'
}
function ReadNow {[void](Message $script:desk 0x111 3046);AwaitRead}
function ClickFirstRow([switch]$Double){
 $list=Control 3019;$scale=[DiagnosticsWin]::GetDpiForWindow($list)/96
 $point=([int](40*$scale) -shl 16) -bor [int](50*$scale)
 [void][DiagnosticsWin]::PostMessage($list,0x201,[IntPtr]1,[IntPtr]$point)
 [void][DiagnosticsWin]::PostMessage($list,0x202,[IntPtr]::Zero,[IntPtr]$point)
 if($Double){[void][DiagnosticsWin]::PostMessage($list,0x203,[IntPtr]1,[IntPtr]$point);[void][DiagnosticsWin]::PostMessage($list,0x202,[IntPtr]::Zero,[IntPtr]$point)}
 Start-Sleep -Milliseconds 150
}
function Capture([string]$name){
 [void][DiagnosticsWin]::SetWindowPos($desk,[IntPtr](-1),0,0,0,0,0x13)
 Start-Sleep -Milliseconds 150
 $r=New-Object DiagnosticsWin+RECT;[void][DiagnosticsWin]::GetWindowRect($script:desk,[ref]$r)
 $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$dc=$graphics.GetHdc();$screen=[DiagnosticsWin]::GetDC([IntPtr]::Zero)
  try{[void][DiagnosticsWin]::BitBlt($dc,0,0,$bitmap.Width,$bitmap.Height,$screen,$r.Left,$r.Top,0x40CC0020)}
  finally{[void][DiagnosticsWin]::ReleaseDC([IntPtr]::Zero,$screen);$graphics.ReleaseHdc($dc)}
  $bitmap.Save((Join-Path $root "output/$name.png"),[Drawing.Imaging.ImageFormat]::Png)
 }finally{$graphics.Dispose();$bitmap.Dispose();[void][DiagnosticsWin]::SetWindowPos($desk,[IntPtr](-2),0,0,0,0,0x13)}
}
function CheckLayout {
 $list=New-Object DiagnosticsWin+RECT;[void][DiagnosticsWin]::GetWindowRect((Control 3019),[ref]$list)
 foreach($id in @(3040,3041,3042,3043,3044,3045,3046,3047,3048,3049,3050,3051,3054,3055,3056,3057,3058,3059,3060,3061,3062,3063,3064)){
  $item=Control $id;if(![DiagnosticsWin]::IsWindowVisible($item)){continue}
  $rect=New-Object DiagnosticsWin+RECT;[void][DiagnosticsWin]::GetWindowRect($item,[ref]$rect)
  if($rect.Bottom -gt $list.Top -or $rect.Right-$rect.Left -lt 50){throw "Diagnostics control $id overlaps the list or has collapsed"}
 }
}
function ExportTable([string]$name){
 $destination=Join-Path $root "output/$name-$PID-$([DateTime]::Now.Ticks).csv"
 [void][DiagnosticsWin]::PostMessage($script:desk,0x111,[IntPtr]3015,[IntPtr]::Zero)
 $dialog=[IntPtr]::Zero
 for($i=0;$i -lt 50;$i++){Start-Sleep -Milliseconds 100;$dialog=[DiagnosticsWin]::FindWindow('#32770','另存为');if($dialog -ne [IntPtr]::Zero){break}}
 if($dialog -eq [IntPtr]::Zero){throw 'CSV save dialog did not open'}
 $fileEdit=[IntPtr]::Zero
 for($i=0;$i -lt 30;$i++){Start-Sleep -Milliseconds 100;$fileEdit=[DiagnosticsWin]::FindFilename($dialog);if($fileEdit -ne [IntPtr]::Zero){break}}
 if($fileEdit -eq [IntPtr]::Zero){throw 'Save filename edit not found'}
 [void][DiagnosticsWin]::SendText($fileEdit,0xC,[IntPtr]::Zero,$destination)
 Start-Sleep -Milliseconds 250
 [void][DiagnosticsWin]::PostMessage($dialog,0x111,[IntPtr]1,[IntPtr]::Zero)
 for($i=0;$i -lt 50;$i++){Start-Sleep -Milliseconds 100;if(Test-Path -LiteralPath $destination){return @(Import-Csv -LiteralPath $destination)}}
 throw "CSV was not exported: $name"
}
$oldDpi=[DiagnosticsWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[DiagnosticsWin]::FindWindow('PicoPet.Win11.Native','PICO');$script:desk=[IntPtr]::Zero;$results=@();$conciseBefore=1
$trackingDir=Join-Path $env:TEMP "pico-tracking-$PID-$([DateTime]::Now.Ticks)";[void](New-Item -ItemType Directory -Path $trackingDir)
try {
 if($pet -eq [IntPtr]::Zero){Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden;Start-Sleep -Milliseconds 700;$pet=[DiagnosticsWin]::FindWindow('PicoPet.Win11.Native','PICO')}
 [void](Message $pet 0x111 243);Start-Sleep -Milliseconds 300
 $script:desk=[DiagnosticsWin]::FindWindow('PicoPet.SystemDesk','PICO 系统 · 本机状态')
 if($desk -eq [IntPtr]::Zero){throw 'Diagnostics module did not open'}
 $conciseBefore=Message (Control 3052) 0xF0
 [void](Message (Control 3052) 0xF1 0);[void](Message $desk 0x111 3052)
 AwaitRead
 [void][DiagnosticsWin]::SetWindowPos($desk,[IntPtr]::Zero,60,50,1920,980,0x14)
 Choose 3040 3;AwaitRead
 if((Rows) -lt 7 -or (Columns) -ne 4){throw 'Security overview is incomplete or not compact'}
 ClickFirstRow
 if((ReadControl 3032) -notmatch '数据来源' -or (ReadControl 3032) -notmatch '原始状态'){throw 'Security overview detail omitted source evidence'}
 CheckLayout;Capture 'diagnostics-security-overview'
 Choose 3040 4;Choose 3059 0;SetText 3061 $root;SetText 3060 'echo PICO_EMBEDDED_CMD';[void](Message $desk 0x111 3062)
 for($i=0;$i -lt 100 -and (ReadControl 3062) -ne '运行';$i++){Start-Sleep -Milliseconds 100}
 if((Rows) -ne 1 -or (Columns) -ne 6){throw 'Embedded command result is missing or not compact'}
 ClickFirstRow
 if((ReadControl 3032) -notmatch 'PICO_EMBEDDED_CMD' -or (ReadControl 3032) -notmatch '退出码'){throw 'Embedded command output or exit status is missing'}
 SetText 3060 'ping -n 10 127.0.0.1 >nul';[void](Message $desk 0x111 3062);Start-Sleep -Milliseconds 300
 if((ReadControl 3062) -ne '停止'){throw 'Long command did not expose a stop action'}
 [void](Message $desk 0x111 3062)
 for($i=0;$i -lt 100 -and (ReadControl 3062) -ne '运行';$i++){Start-Sleep -Milliseconds 100}
 if((Rows) -ne 2){throw 'Stopped command was not retained in history'}
 CheckLayout;Capture 'diagnostics-command-assistant'
 [void](Message $desk 0x111 3064);if((Rows) -ne 0){throw 'Command history did not clear'}
 if($Codex){
  Choose 3059 1;SetText 3060 'Reply with exactly PICO_CODEX_OK and no other text.';[void](Message $desk 0x111 3062)
  for($i=0;$i -lt 1200 -and (ReadControl 3062) -ne '运行';$i++){Start-Sleep -Milliseconds 100}
  if((ReadControl 3062) -ne '运行'){throw 'Embedded Codex did not complete within 120 seconds'}
  ClickFirstRow;$codexDetail=ReadControl 3032
  if($codexDetail -notmatch 'PICO_CODEX_OK' -or $codexDetail -notmatch '状态\s+完成' -or $codexDetail -notmatch '退出码\s+0'){throw "Embedded Codex failed: $codexDetail"}
  [void](Message $desk 0x111 3064)
 }
 Choose 3040 0;Choose 3041 0;Choose 3042 3;Choose 3043 0;AwaitRead
 if((Columns) -ne 5 -or (Rows) -le 0){throw 'System event overview is empty or not compact'}
 $completeEventRows=Rows;[void](Message (Control 3052) 0xF1 1);[void](Message $desk 0x111 3052);$conciseEventRows=Rows
 if($conciseEventRows -gt $completeEventRows){throw 'Concise event projection exceeded its source data'}
 [void](Message (Control 3052) 0xF1 0);[void](Message $desk 0x111 3052)
 CheckLayout;Capture 'diagnostics-events-overview'
 ClickFirstRow
 $detail=ReadControl 3032
 if($detail -notmatch 'Windows 原文' -or $detail -notmatch '原始 XML' -or $detail -notmatch '<Event '){throw 'Event inspector dropped original evidence'}
 Capture 'diagnostics-events-details-wide'
 $logs=@(ExportTable 'diagnostics-events')
 if($logs.Count -gt 300 -or @($logs[0].PSObject.Properties).Count -ne 11 -or $logs[0].'原始 XML' -notmatch '<Event '){throw 'Event CSV is incomplete'}
 $eventId=$logs[0].'事件 ID';SetText 3044 $eventId;ReadNow
 $matching=@(ExportTable 'diagnostics-event-id')
 if(!$matching.Count -or @($matching | Where-Object {$_.'事件 ID' -ne $eventId}).Count){throw 'Event ID query did not filter'}
 SetText 3044 '';Choose 3043 2;AwaitRead
 $errors=@(ExportTable 'diagnostics-errors')
 if(@($errors | Where-Object {$_.'级别' -notin @('错误','严重')}).Count){throw 'Severity filter failed'}
 Choose 3041 1;AwaitRead
 Choose 3043 0;AwaitRead
 SetText 3045 '__pico_no_such_event_7f6b91__';ReadNow
 if((Rows) -ne 0){throw 'Event keyword filter failed'}
 SetText 3044 '65536';ReadNow
 if((ReadControl 3020) -notmatch '65535'){throw 'Invalid event ID was not explained'}
 Choose 3040 1;AwaitRead
 if((Rows) -le 0 -or (Columns) -ne 4){throw 'Invalid hidden log ID blocked registry mode'}
 SetText 3044 '';Choose 3050 7;AwaitRead
 SetText 3045 'CurrentBuildNumber';ReadNow
 $registry=@(ExportTable 'diagnostics-registry-value')
 if($registry.Count -ne 1 -or $registry[0].'键 / 值名称' -ne 'CurrentBuildNumber' -or @($registry[0].PSObject.Properties).Count -ne 10){throw 'Registry filter or full CSV fields failed'}
 ClickFirstRow
 if((ReadControl 3032) -notmatch '键最后写入时间' -or (ReadControl 3032) -notmatch 'REG_SZ'){throw 'Registry detail metadata missing'}
 Choose 3048 1;AwaitRead
 $wow=@(ExportTable 'diagnostics-registry-32')
 if($wow.Count -ne 1 -or $wow[0].'视图' -ne '32 位'){throw 'Registry view did not change'}
 Choose 3048 0;AwaitRead
 SetText 3047 'HKCU';SetText 3045 '';ReadNow
 ClickFirstRow -Double;AwaitRead
 if((ReadControl 3047) -notmatch '^HKEY_CURRENT_USER\\'){throw 'Registry child double-click failed'}
 [void](Message $desk 0x111 3049);AwaitRead
 if((ReadControl 3047) -ne 'HKEY_CURRENT_USER'){throw 'Registry Up navigation failed'}
 SetText 3047 'HKCU\PICO-No-Such-Key-7f6b91';ReadNow
 if((Rows) -ne 0 -or (ReadControl 3020) -notmatch '无法打开注册表'){throw 'Missing registry key did not report read error'}
 for($i=0;$i -lt 6;$i++){Choose 3040 0;Choose 3041 ($i%2);Choose 3040 1;Choose 3050 7}
 SetText 3045 'CurrentBuildNumber';ReadNow
 if((Columns) -ne 4 -or (Rows) -ne 1){throw 'Superseded query overwrote latest registry result'}
 Choose 3040 0;[void](Message $desk 0x111 3051);AwaitRead
 Choose 3040 1;SetText 3045 '';Choose 3050 5;AwaitRead
 ClickFirstRow;CheckLayout;Capture 'diagnostics-registry-wide'
 [void](Message (Control 3030) 0xF1 1);[void](Message $desk 0x111 3030)
 if((Columns) -ne 10){throw 'Full registry columns missing'}
 [void](Message (Control 3030) 0xF1 0);[void](Message $desk 0x111 3030)
 [void][DiagnosticsWin]::SetWindowPos($desk,[IntPtr]::Zero,60,50,1240,850,0x14)
 CheckLayout;Capture 'diagnostics-registry-narrow'
 Choose 3040 0;Choose 3041 0;AwaitRead;ClickFirstRow;CheckLayout;Capture 'diagnostics-events-narrow'
 Choose 3040 2;SetText 3054 $trackingDir;[void](Message $desk 0x111 3056);Start-Sleep -Milliseconds 250
 $trackingProcess=Start-Process -FilePath $env:ComSpec -ArgumentList @('/d','/c','ping -n 4 127.0.0.1 >nul') -WindowStyle Hidden -PassThru
 SetText 3045 'cmd.exe'
 for($i=0;$i -lt 40 -and (Rows)-eq 0;$i++){Start-Sleep -Milliseconds 100}
 if((Rows) -le 0){throw 'Security tracking did not capture a program start'}
 [void]$trackingProcess.WaitForExit(6000)
 for($i=0;$i -lt 30 -and (Rows)-lt 2;$i++){Start-Sleep -Milliseconds 100}
 if((Rows) -lt 2){throw 'Security tracking did not capture the program exit'}
 $trackingCpuProcess=Get-Process PicoPet;$trackingCpuBefore=$trackingCpuProcess.TotalProcessorTime.TotalSeconds;Start-Sleep -Seconds 3;$trackingCpuProcess=Get-Process -Id $trackingCpuProcess.Id;$trackingCpuSeconds=$trackingCpuProcess.TotalProcessorTime.TotalSeconds-$trackingCpuBefore
 $trackedFile=Join-Path $trackingDir 'tracked.ps1';Set-Content -LiteralPath $trackedFile -Value 'Write-Output test' -Encoding ascii
 SetText 3045 'tracked.ps1'
 for($i=0;$i -lt 40 -and (Rows)-eq 0;$i++){Start-Sleep -Milliseconds 100}
 if((Rows) -le 0 -or (Columns) -ne 6){throw 'Security tracking did not show the file event in compact columns'}
 ClickFirstRow
 if((ReadControl 3032) -notmatch '无法可靠归因' -or (ReadControl 3032) -notmatch '关注'){throw 'Security tracking detail omitted attribution or attention evidence'}
 $tracking=@(ExportTable 'diagnostics-security-tracking')
 if(!$tracking.Count -or @($tracking[0].PSObject.Properties).Count -ne 10 -or $tracking[0].'对象' -notmatch 'tracked.ps1'){throw 'Security tracking CSV is incomplete'}
 CheckLayout;Capture 'diagnostics-security-tracking'
 [void](Message $desk 0x111 3056);[void](Message $desk 0x111 3057);SetText 3045 ''
 if((Rows) -ne 0 -or (ReadControl 3056) -ne '开始追踪'){throw 'Security tracking did not stop and clear cleanly'}
 $process=Get-Process PicoPet;$cpu=$process.TotalProcessorTime.TotalSeconds
 Start-Sleep -Seconds 3;$process=Get-Process -Id $process.Id
 $results+=@{securityOverview=$true;embeddedCommand=$true;commandStop=$true;eventRows=$logs.Count;conciseEventRows=$conciseEventRows;eventId=$eventId;filteredRows=$matching.Count;registryNavigation=$true;securityTracking=$true;trackingProgramLifecycle=$true;trackingCpuSeconds=$trackingCpuSeconds;trackingCsvFields=10;fullCsvFields=$true;supersededQueries=$true;responsiveLayouts=$true;idleCpuSeconds=$process.TotalProcessorTime.TotalSeconds-$cpu;workingSetMB=[Math]::Round($process.WorkingSet64/1MB,2)}
 [void][DiagnosticsWin]::ShowWindow($desk,6)
 $cpu=$process.TotalProcessorTime.TotalSeconds;Start-Sleep -Seconds 3;$process=Get-Process -Id $process.Id
 $results+=@{minimizedCpuSeconds=$process.TotalProcessorTime.TotalSeconds-$cpu}
 [void][DiagnosticsWin]::ShowWindow($desk,9)
 $results | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $root 'output/diagnostics-test.json') -Encoding utf8
 $results | ConvertTo-Json -Depth 5
}finally{
 $dialog=[DiagnosticsWin]::FindWindow('#32770','另存为');if($dialog -ne [IntPtr]::Zero){[void][DiagnosticsWin]::PostMessage($dialog,0x111,[IntPtr]2,[IntPtr]::Zero)}
 if($script:desk -ne [IntPtr]::Zero){[void](Message (Control 3052) 0xF1 $conciseBefore);[void](Message $desk 0x111 3052)}
 $trackedFile=Join-Path $trackingDir 'tracked.ps1';if(Test-Path -LiteralPath $trackedFile){Remove-Item -LiteralPath $trackedFile -Force};if(Test-Path -LiteralPath $trackingDir){Remove-Item -LiteralPath $trackingDir -Force}
 [void][DiagnosticsWin]::SetThreadDpiAwarenessContext($oldDpi)
}
