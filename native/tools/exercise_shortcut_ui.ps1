$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Monitor
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ShortcutCheck {
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
}
'@
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini'
$shortcutConfig=Join-Path $env:LOCALAPPDATA 'PicoPet/shortcuts.ini'
if((Test-Path $shortcutConfig) -and (Get-Content $shortcutConfig -Raw) -match 'count=[1-9]'){throw 'This fixture requires an empty shortcut list'}
$saved=@{};foreach($line in Get-Content $config){if($line -match '^(size|view|yaw|pitch|x|y|hd)=(.*)$'){$saved[$matches[1]]=$matches[2]}}
$marker=Join-Path $root "output/shortcut-launched-$PID.txt"
$fixture=Join-Path $root "output/桌面快捷测试-$PID.lnk"
$shell=New-Object -ComObject WScript.Shell;$link=$shell.CreateShortcut($fixture)
$link.TargetPath=$env:ComSpec;$link.Arguments='/d /c echo PICO_SHORTCUT_OK>"'+$marker+'"';$link.WorkingDirectory=(Join-Path $root 'output');$link.Save()
function AwaitWindow([string]$name){for($i=0;$i -lt 50;$i++){$h=[MonitorWin]::FindWindow('#32770',$name);if($h -ne [IntPtr]::Zero){return $h};Start-Sleep -Milliseconds 100};throw "Dialog did not open: $name"}
function MoveShortcut {
 $r=New-Object MonitorWin+RECT;[void][MonitorWin]::GetWindowRect($pet,[ref]$r);$extent=$r.Right-$r.Left
 $worldX=(118/800.0-.5)*83.6;$worldY=42.5+(.5-293/500.0)*47.6
 [void][ShortcutCheck]::SetCursorPos(($r.Left+[int]($extent*(.5+$worldX/120))),($r.Top+[int]($extent*(.5+(44-$worldY)/120))))
 [void](Message $pet 0x200);Start-Sleep -Milliseconds 180
 if((Message $pet 0x8003 16) -ne 4){throw 'Saved shortcut is not selectable in the screen grid'}
}
try {
 [void](Message $pet 0x111 210);[void](Message $pet 0x111 250);[void](Message $pet 0x111 261)
 [void][ShortcutCheck]::SetForegroundWindow($pet)
 [void][MonitorWin]::PostMessage($pet,0x111,[IntPtr]302,[IntPtr]::Zero)
 $dialog=AwaitWindow '添加程序、文件或 Windows 快捷方式'
 Start-Sleep -Milliseconds 900
 $edit=[IntPtr]::Zero;for($i=0;$i -lt 30;$i++){$edit=[MonitorWin]::FindFilename($dialog);if($edit -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
 if($edit -eq [IntPtr]::Zero){throw 'Open filename edit missing'}
 [void][MonitorWin]::SendText($edit,0xC,[IntPtr]::Zero,$fixture)
 Start-Sleep -Milliseconds 300
 [void][MonitorWin]::PostMessage([MonitorWin]::GetDlgItem($dialog,1),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)
 for($i=0;$i -lt 70;$i++){if((Test-Path $shortcutConfig) -and (Get-Content $shortcutConfig -Raw) -match [regex]::Escape($fixture)){break};Start-Sleep -Milliseconds 100}
 if(!(Test-Path $shortcutConfig) -or (Get-Content $shortcutConfig -Raw) -notmatch [regex]::Escape($fixture)){$script:desk=$dialog;Capture 'shortcut-picker-failure';if(Test-Path $shortcutConfig){Get-Content $shortcutConfig};throw 'Selected .lnk was not saved intact'}
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden;Start-Sleep -Milliseconds 600
 $pet=[MonitorWin]::FindWindow('PicoPet.Win11.Native','PICO');MoveShortcut
 $script:desk=$pet;Capture 'desktop-custom-shortcut'
 [void](Message $pet 0x201);[void](Message $pet 0x202)
 for($i=0;$i -lt 50;$i++){if(Test-Path $marker){break};Start-Sleep -Milliseconds 100}
 if(!(Test-Path $marker) -or (Get-Content $marker -Raw) -notmatch 'PICO_SHORTCUT_OK'){throw 'Windows shortcut arguments were not honored'}
 MoveShortcut
 [void](Message $pet 0x111 304);Start-Sleep -Milliseconds 350
 if((Get-Content $shortcutConfig -Raw) -notmatch 'count=0' -or !(Test-Path $fixture)){throw 'Removing shortcut did not preserve the target or save changes'}
 @{nativePicker=$true;unicodePath=$true;restartPersistence=$true;lnkArguments=$true;removePreservesTarget=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/shortcut-ui-test.json')
 Get-Content (Join-Path $root 'output/shortcut-ui-test.json')
} finally {
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 foreach($entry in $saved.GetEnumerator()){[void][ShortcutCheck]::WritePrivateProfileString('PICO',$entry.Key,$entry.Value,$config)}
 # Only remove this test's entry if the failure left it behind.
 if((Test-Path $shortcutConfig) -and (Get-Content $shortcutConfig -Raw) -match [regex]::Escape($fixture)){[void][ShortcutCheck]::WritePrivateProfileString('Shortcuts','count','0',$shortcutConfig);[void][ShortcutCheck]::WritePrivateProfileString('Shortcuts','item0',$null,$shortcutConfig)}
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 Remove-Item -LiteralPath $fixture -ErrorAction SilentlyContinue
 [void][MonitorWin]::SetThreadDpiAwarenessContext($oldDpi)
}
