$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ZoomMenu {
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
 [void](Send ([EmbeddedWin]::FindClass('PicoPet.Preferences')) 0x10)
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
 @{desktopZooms=40;terminalZooms=8;terminalPreservesSize=$true;intermediateExpressionFrames=0;completeScreenMenu=$true;settingsGear=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/zoom-menu-checks.json')
 Get-Content (Join-Path $root 'output/zoom-menu-checks.json')
} finally {
 if($pet -ne [IntPtr]::Zero){[void][ZoomMenu]::PostMessage($pet,0x1F,[IntPtr]::Zero,[IntPtr]::Zero)}
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 Copy-Item -LiteralPath $backup -Destination $config -Force
 Start-Process -FilePath $restart -WindowStyle Hidden
 [void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y);[void][EmbeddedWin]::SetThreadDpiAwarenessContext($oldDpi)
}
