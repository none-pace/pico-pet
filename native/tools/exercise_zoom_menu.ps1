$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
Add-Type -AssemblyName System.Windows.Forms
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ZoomMenu {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string section,string key,string value,string file);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr FindWindow(string c,string t);
 public static IntPtr Popup(){return FindWindow("#32768",null);}
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr GetSubMenu(IntPtr h,int i);
 [DllImport("user32.dll")]public static extern int GetMenuItemCount(IntPtr h);
 [DllImport("user32.dll")]public static extern uint GetMenuItemID(IntPtr h,int i);
 public static bool Has(IntPtr menu,uint id){for(int i=0;i<GetMenuItemCount(menu);i++){if(GetMenuItemID(menu,i)==id)return true;var sub=GetSubMenu(menu,i);if(sub!=IntPtr.Zero && Has(sub,id))return true;}return false;}
}
'@
function Await([scriptblock]$condition,[string]$failure){for($i=0;$i -lt 80;$i++){if(& $condition){return};Start-Sleep -Milliseconds 100};throw $failure}
function MoveScreen([int]$x=400,[int]$y=240){
 $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r);$extent=$r.Right-$r.Left
 $worldX=($x/800.0-.5)*83.6;$worldY=42.5+(.5-$y/500.0)*47.6
 [void][EmbeddedWin]::SetCursorPos(($r.Left+[int]($extent*(.5+$worldX/120))),($r.Top+[int]($extent*(.5+(44-$worldY)/120))))
 [void](Send $pet 0x200);Start-Sleep -Milliseconds 180
}
$oldDpi=[EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4));$cursor=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini';$backup=Join-Path $root "output/settings-before-zoom-$PID.ini"
$oldProcess=Get-Process PicoPet -ErrorAction SilentlyContinue|Select-Object -First 1
$restart=if($oldProcess){$oldProcess.Path}else{Join-Path $root 'dist/PicoPet.exe'}
& (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
Copy-Item -LiteralPath $config -Destination $backup
$runPath='Software\Microsoft\Windows\CurrentVersion\Run';$runName='PicoPet.Win11'
$runKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($runPath)
$runSaved=if($runKey){$runKey.GetValue($runName,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}else{$null}
$runKind=if($null -ne $runSaved){$runKey.GetValueKind($runName)}else{[Microsoft.Win32.RegistryValueKind]::String}
if($runKey){$runKey.Dispose()}
$pet=[IntPtr]::Zero
try {
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 Await { $script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO');$pet -ne [IntPtr]::Zero } 'Pet did not start'
 if((Send $pet 0x8003 2) -band 4){[void](Send $pet 0x111 101)}
 if((Send $pet 0x8003 4) -band 1){[void](Send $pet 0x111 108)}
 [void](Send $pet 0x111 210);[void](Send $pet 0x111 250);[void](Send $pet 0x111 106)
 foreach($quality in @(261,260)){
  [void](Send $pet 0x111 $quality);MoveScreen
  if((Send $pet 0x8003 14) -ne 1){throw 'Desktop not visible before zoom'}
  $exits=Send $pet 0x8003 65
  for($i=0;$i -lt 20;$i++){
   $delta=if(($i%10) -lt 5){120}else{-120}
   [void](Send $pet 0x20A ($delta -shl 16))
   if((Send $pet 0x8003 64) -lt 7 -or (Send $pet 0x8003 14) -ne 1){throw "Screen reverted to an expression during zoom in quality $quality"}
   if((Send $pet 0x8003 58) -ne 0){throw 'Resize returned before a complete frame was ready'}
  }
  if((Send $pet 0x8003 65) -ne $exits){throw 'Zoom presented an intermediate non-desktop frame'}
 }
 [void](Send $pet 0x111 261);MoveScreen 143 465
 if((Send $pet 0x8003 16) -ne 1004){throw 'Settings gear hit target missing'}
 [void](Send $pet 0x201);[void](Send $pet 0x202)
 Await { [EmbeddedWin]::FindClass('PicoPet.Preferences') -ne [IntPtr]::Zero } 'Settings gear did not open preferences'
 $prefs=[EmbeddedWin]::FindClass('PicoPet.Preferences')
 function SelectPreference([int]$id,[int]$index){[void](Send ([EmbeddedWin]::GetDlgItem($prefs,$id)) 0x14e $index);[void](Send $prefs 0x111 ($id -bor (1 -shl 16)))}
 SelectPreference 1021 0
 if((Get-ItemProperty -Path ('HKCU:\'+$runPath) -ErrorAction SilentlyContinue).$runName){throw 'Startup disable left the Run entry'}
 SelectPreference 1021 1
 $expected='"'+(Join-Path $root 'dist\PicoPet.exe')+'" --startup'
 if((Get-ItemPropertyValue -Path ('HKCU:\'+$runPath) -Name $runName) -ne $expected){throw 'Startup command not correctly quoted or saved'}
 $displays=[Windows.Forms.Screen]::AllScreens | Sort-Object DeviceName
 $displayCount=Send ([EmbeddedWin]::GetDlgItem($prefs,930)) 0x146
 if($displayCount -lt 2){throw 'Connected startup monitor missing'}
 $target=$displays[-1]
 $targetIndex=1
 for($i=1;$i -lt $displayCount;$i++){
  $text=New-Object Text.StringBuilder 1024
  [void][EmbeddedWin]::ReadText([EmbeddedWin]::GetDlgItem($prefs,930),0x148,[IntPtr]$i,$text)
  $displayNumber=$target.DeviceName -replace '^.*DISPLAY',''
  if($text.ToString().StartsWith('屏幕 '+$displayNumber+' ·')){$targetIndex=$i;break}
 }
 SelectPreference 930 $targetIndex
 if(!((Get-Content -LiteralPath $config) -contains ('startupDisplay='+$target.DeviceName))){throw 'Startup display selection not persisted'}
 [void](Send $prefs 0x10)
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -ArgumentList '--startup' -WindowStyle Hidden
 Await {$script:pet=[EmbeddedWin]::FindClass('PicoPet.Win11.Native');$pet -ne [IntPtr]::Zero} 'Startup launch failed'
 $placed=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$placed)
 if(!$target.Bounds.Contains([int](($placed.Left+$placed.Right)/2),[int](($placed.Top+$placed.Bottom)/2))){throw 'Launch did not use selected monitor'}
 $duplicate=Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -ArgumentList '--startup' -WindowStyle Hidden -PassThru
 if(!$duplicate.WaitForExit(5000)){throw 'Duplicate startup instance did not exit'}
 $unchanged=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$unchanged)
 if($placed.Left -ne $unchanged.Left -or $placed.Top -ne $unchanged.Top){throw 'Duplicate startup launch moved the pet'}
 [void](Send $pet 0x111 300);$prefs=[EmbeddedWin]::FindClass('PicoPet.Preferences')
 if((Send ([EmbeddedWin]::GetDlgItem($prefs,1021)) 0x147) -ne 1){throw 'Startup registration not reflected after restart'}
 SelectPreference 1021 0;SelectPreference 930 0
 [void](Send $prefs 0x10)
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 [void][ZoomMenu]::WritePrivateProfileString('PICO','startupDisplay','\\.\DISPLAY_MISSING_TEST',$config)
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -ArgumentList '--startup' -WindowStyle Hidden
 Await {$script:pet=[EmbeddedWin]::FindClass('PicoPet.Win11.Native');$pet -ne [IntPtr]::Zero} 'Missing-display startup failed'
 $fallback=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$fallback)
 if(![Windows.Forms.Screen]::PrimaryScreen.Bounds.Contains([int](($fallback.Left+$fallback.Right)/2),[int](($fallback.Top+$fallback.Bottom)/2))){throw 'Disconnected monitor did not fall back to primary'}
 [void](Send $pet 0x111 300);$prefs=[EmbeddedWin]::FindClass('PicoPet.Preferences')
 [void](Send $prefs 0x111 931)
 [void](Send $prefs 0x10)
 if(!((Get-Content -LiteralPath $config) -contains 'startupDisplay=\\.\DISPLAY_MISSING_TEST')){throw 'Disconnected display preference was discarded'}
 [void](Send $pet 0x111 300);$prefs=[EmbeddedWin]::FindClass('PicoPet.Preferences');SelectPreference 930 0;[void](Send $prefs 0x10)
 [void](Send $pet 0x111 106)
 MoveScreen 118 145
 $point=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$point)
 [void][ZoomMenu]::PostMessage($pet,0x7B,$pet,[IntPtr](($point.Y -shl 16) -bor ($point.X -band 0xffff)))
 Await { [ZoomMenu]::Popup() -ne [IntPtr]::Zero } 'Screen context menu did not open'
 $popup=[ZoomMenu]::Popup();$menu=[EmbeddedWin]::SendMessage($popup,0x1E1,[IntPtr]::Zero,[IntPtr]::Zero)
 foreach($id in @(300,302,303,260,261,290,291,292,293,310,240,241,242,243,244,102,107,4000)){if(![ZoomMenu]::Has($menu,$id)){throw "Screen menu missing command $id"}}
 Add-Type -AssemblyName System.Windows.Forms
 $bounds=[Windows.Forms.Screen]::PrimaryScreen.Bounds;$bitmap=New-Object Drawing.Bitmap $bounds.Width,$bounds.Height;$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($bounds.Left,$bounds.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $root 'output/screen-full-context-menu.png'))}finally{$graphics.Dispose();$bitmap.Dispose()}
 [void][ZoomMenu]::PostMessage($pet,0x1F,[IntPtr]::Zero,[IntPtr]::Zero)
 Await { [ZoomMenu]::Popup() -eq [IntPtr]::Zero } 'Context menu failed to close'
 [void](Send $pet 0x111 201)
 $beforeConsole=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$beforeConsole)
 [void](Send $pet 0x111 244)
 $afterConsole=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$afterConsole)
 if($beforeConsole.Left -ne $afterConsole.Left -or $beforeConsole.Top -ne $afterConsole.Top -or $beforeConsole.Right -ne $afterConsole.Right -or $beforeConsole.Bottom -ne $afterConsole.Bottom){throw 'Opening the terminal changed the user-selected size or position'}
 TypeText 'echo PICO_TV_CMD_OK';[void](Send $pet 0x102 13)
 Await { (Send $pet 0x8003 28) -eq 1 } 'Console output not ready'
 MoveScreen
 for($i=0;$i -lt 8;$i++){[void](Send $pet 0x20A ($(if($i%2){-120}else{120}) -shl 16));if((Send $pet 0x8003 25) -ne 1 -or (Send $pet 0x8003 64) -ne 11){throw 'Console disappeared during zoom'}}
 [void](Send $pet 0x111 244)
 @{startupRegistration=$true;startupDisplay=$true;startupSingleInstance=$true;disconnectedDisplayFallback=$true;disconnectedSelectionRetained=$true;desktopZooms=40;terminalZooms=8;terminalPreservesSize=$true;intermediateExpressionFrames=0;completeScreenMenu=$true;settingsGear=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/zoom-menu-checks.json')
 Get-Content (Join-Path $root 'output/zoom-menu-checks.json')
} finally {
 if($pet -ne [IntPtr]::Zero){[void][ZoomMenu]::PostMessage($pet,0x1F,[IntPtr]::Zero,[IntPtr]::Zero)}
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 $restoreRun=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey($runPath)
 try{if($null -ne $runSaved){$restoreRun.SetValue($runName,$runSaved,$runKind)}else{$restoreRun.DeleteValue($runName,$false)}}finally{$restoreRun.Dispose()}
 Copy-Item -LiteralPath $backup -Destination $config -Force
 Start-Process -FilePath $restart -WindowStyle Hidden
 [void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y);[void][EmbeddedWin]::SetThreadDpiAwarenessContext($oldDpi)
}
