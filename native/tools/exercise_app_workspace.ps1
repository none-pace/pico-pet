param([switch]$Browser)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
Add-Type @'
using System;using System.Runtime.InteropServices;using System.Text;
public static class AppCheck {
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string section,string key,string value,string path);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr h,out RECT r);
 public static RECT Content(IntPtr window){var rect=new RECT();EnumChildWindows(window,(h,p)=>{var c=new StringBuilder(128);GetClassName(h,c,128);if(c.ToString()=="Chrome_RenderWidgetHostHWND"){GetWindowRect(h,out rect);return false;}return true;},IntPtr.Zero);return rect;}
 [DllImport("user32.dll")]static extern bool EnumChildWindows(IntPtr h,EnumProc e,IntPtr p);
 [DllImport("user32.dll")]static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr h,StringBuilder s,int n);
 public static IntPtr Filename(IntPtr dialog){IntPtr found=IntPtr.Zero;EnumChildWindows(dialog,(h,p)=>{var c=new StringBuilder(128);GetClassName(h,c,128);if(c.ToString()=="Edit" && (GetDlgCtrlID(h)==1001 || GetDlgCtrlID(h)==1148)){found=h;return false;}return true;},IntPtr.Zero);return found;}

 [DllImport("user32.dll")]public static extern IntPtr SetWindowLongPtr(IntPtr h,int i,IntPtr v);
 [DllImport("user32.dll")]public static extern bool SetLayeredWindowAttributes(IntPtr h,uint c,byte a,uint f);

 delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")]static extern bool EnumWindows(EnumProc e,IntPtr p);
 [DllImport("user32.dll")]static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 public static IntPtr Find(uint target){IntPtr found=IntPtr.Zero;EnumProc match=(h,p)=>{uint id;GetWindowThreadProcessId(h,out id);if(id==target){var s=new StringBuilder(256);GetWindowText(h,s,256);if(s.ToString().StartsWith("PICO Application")){found=h;return false;}}return true;};EnumWindows((h,p)=>{if(!match(h,p))return false;EnumChildWindows(h,match,p);return found==IntPtr.Zero;},IntPtr.Zero);return found;}
 public static string Title(IntPtr h){var b=new StringBuilder(256);GetWindowText(h,b,256);return b.ToString();}
 public static IntPtr Next(IntPtr p,IntPtr a){return FindWindowEx(p,a,null,null);}

 [DllImport("user32.dll")]public static extern IntPtr GetParent(IntPtr h);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int i);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindowEx(IntPtr p,IntPtr a,string c,string t);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int z,uint f);
 public static uint ProcessId(IntPtr h){uint id;GetWindowThreadProcessId(h,out id);return id;}
}
'@
function Await([scriptblock]$condition,[string]$failure){for($i=0;$i -lt 100;$i++){if(& $condition){return};Start-Sleep -Milliseconds 100};throw $failure}
$oldDpi=[EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$fixture=Join-Path $root "output/app-fixture-$PID.exe"
Add-Type -OutputAssembly $fixture -OutputType WindowsApplication -ReferencedAssemblies System.Windows.Forms,System.Drawing -TypeDefinition @'
using System;using System.Windows.Forms;using System.Drawing;
public static class AppFixture {
 [STAThread]public static void Main(string[] args){if(args.Length>0)System.IO.File.WriteAllText(args[0],"PICO_SHORTCUT_ARGS_OK");Application.EnableVisualStyles();var form=new Form{Text="PICO Application Fixture",Size=new Size(600,380),StartPosition=FormStartPosition.Manual,Location=new Point(100,100),BackColor=Color.FromArgb(32,92,126)};
 var edit=new TextBox{Location=new Point(60,70),Size=new Size(400,32),Font=new Font("Segoe UI",16)};
 var button=new Button{Text="Change colour",Location=new Point(60,140),Size=new Size(180,48)};
 button.Click+=(s,e)=>{form.BackColor=Color.FromArgb(155,45,72);form.Text="PICO Application Clicked";};form.Controls.Add(edit);form.Controls.Add(button);Application.Run(form);}
}
'@
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini'
$backup=Join-Path $root "output/settings-before-app-$PID.ini"
$running=Get-Process PicoPet -ErrorAction SilentlyContinue | Where-Object MainWindowTitle -eq 'PICO' | Select-Object -First 1
$restart=Join-Path $env:LOCALAPPDATA 'Programs/PicoPet/PicoPet.exe'
& (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
Copy-Item -LiteralPath $config -Destination $backup -Force
$pet=[IntPtr]::Zero;$app=$null;$second=$null;$appWindow=[IntPtr]::Zero
try {
 [void][AppCheck]::WritePrivateProfileString('PICO','appResolution','0',$config);[void][AppCheck]::WritePrivateProfileString('PICO','appCanvasVersion','1',$config)
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 Await {$script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO');$pet -ne [IntPtr]::Zero} 'Pet did not start'
 if((Send $pet 0x8003 2) -band 4){[void](Send $pet 0x111 101)}
 if((Send $pet 0x8003 4) -band 1){[void](Send $pet 0x111 108)}
 [void](Send $pet 0x111 210);[void](Send $pet 0x111 250);[void](Send $pet 0x111 261)
 [void](Send $pet 0x111 324)
 [void](Send $pet 0x111 320)
 Await { (Send $pet 0x8003 80) -ne 0 } 'Application host did not start'
 $argumentFile=Join-Path $root "output/shortcut args $PID.txt"
 $shortcutPath=[IO.Path]::GetFullPath((Join-Path $root "output/application shortcut $PID.lnk"))
 $shell=New-Object -ComObject WScript.Shell;$shortcut=$shell.CreateShortcut($shortcutPath);$shortcut.TargetPath=$fixture;$shortcut.Arguments='"'+$argumentFile+'"';$shortcut.WorkingDirectory=Split-Path $fixture;$shortcut.Save()
 [void][AppCheck]::PostMessage($pet,0x111,[IntPtr]322,[IntPtr]0)
 Await {$script:openDialog=[EmbeddedWin]::FindWindow('#32770','选择程序或桌面快捷方式 · 启动后自动接入');$openDialog -ne [IntPtr]::Zero} 'Application chooser missing'
 Await {$script:filename=[AppCheck]::Filename($openDialog);$filename -ne [IntPtr]::Zero} 'Filename field missing'
 Start-Sleep -Milliseconds 700
 [void][EmbeddedWin]::SendMessage($filename,0xB1,[IntPtr]0,[IntPtr](-1));[void][EmbeddedWin]::SendText($filename,0xC2,[IntPtr]1,('"'+$shortcutPath+'"'))
 Start-Sleep -Milliseconds 250
 [void][AppCheck]::PostMessage([EmbeddedWin]::GetDlgItem($openDialog,1),0xF5,[IntPtr]0,[IntPtr]0)
 Await {$script:app=Get-Process ([IO.Path]::GetFileNameWithoutExtension($fixture)) -ErrorAction SilentlyContinue | Select-Object -First 1;$null -ne $app} 'Shortcut failed to start application'
 Await {Test-Path -LiteralPath $argumentFile} 'Shortcut arguments lost'
 if((Get-Content -LiteralPath $argumentFile -Raw) -ne 'PICO_SHORTCUT_ARGS_OK'){throw 'Incorrect shortcut arguments'}
 Await {$script:appWindow=[AppCheck]::Find($app.Id);$appWindow -ne [IntPtr]::Zero} 'Fixture missing'
 [void][AppCheck]::SetWindowPos($pet,[IntPtr](-1),900,220,0,0,0x11)
 $frames=Send $pet 0x8003 83
 Await { (Send $pet 0x8003 82) -eq 1 } 'App was not attached'
 $hostWindow=[IntPtr](Send $pet 0x8003 80)
 if([AppCheck]::GetParent($appWindow) -ne $hostWindow){throw 'App is not a child of the container'}
 Await {(Send $pet 0x8003 83) -gt $frames} 'No captured application frames'
 Start-Sleep -Milliseconds 600
 Write-Host "Render frames $(Send $pet 0x8003 0); state $(Send $pet 0x8003 2); app frames $(Send $pet 0x8003 83)"
 $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
 $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($r.Left,$r.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $root 'output/app-workspace.png'))
  $blue=0;for($y=0;$y -lt $bitmap.Height;$y+=4){for($x=0;$x -lt $bitmap.Width;$x+=4){$c=$bitmap.GetPixel($x,$y);if([Math]::Abs($c.R-32) -lt 10 -and [Math]::Abs($c.G-92) -lt 10 -and [Math]::Abs($c.B-126) -lt 10){$blue++}}};if($blue -lt 100){throw 'Captured app content is blank or wrong'}}finally{$graphics.Dispose();$bitmap.Dispose()}
 function ClickScreen([int]$x,[int]$y){
  $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r);$extent=$r.Right-$r.Left
  $worldX=($x/800.0-.5)*83.6;$worldY=42.5+(.5-$y/500.0)*47.6
  $cx=[int]($extent*(.5+$worldX/120));$cy=[int]($extent*(.5+(44-$worldY)/120));$coord=[IntPtr](($cy -shl 16) -bor ($cx -band 0xffff))
  [void][EmbeddedWin]::SendMessage($pet,0x201,[IntPtr]1,$coord);[void][EmbeddedWin]::SendMessage($pet,0x202,[IntPtr]0,$coord)
 }
 ClickScreen 150 124
 foreach($char in 'TV_INPUT_OK'.ToCharArray()){[void](Send $pet 0x102 ([int]$char))}
 $edit=[AppCheck]::Next($appWindow,[IntPtr]::Zero)
 function HasInput([string]$expected='TV_INPUT_OK') {
  $child=[IntPtr]::Zero
  while(($child=[AppCheck]::Next($appWindow,$child)) -ne [IntPtr]::Zero){$text=New-Object Text.StringBuilder 512;[void][EmbeddedWin]::ReadText($child,0xD,[IntPtr]512,$text);if($text.ToString() -eq $expected){return $true}}
  return $false
 }
 Await {HasInput} 'Projected typing did not reach the app'
 ClickScreen 140 205
 Await {[AppCheck]::Title($appWindow) -eq 'PICO Application Clicked'} 'Projected button click did not reach the app'
 $expected='TV_INPUT_OK'
 foreach($resolution in @(@(1,1280,730),@(2,1600,912))){
  [void][EmbeddedWin]::SendMessage($hostWindow,0x801f,[IntPtr]($resolution[0] -shl 16),[IntPtr]15)
  $bounds=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($appWindow,[ref]$bounds)
  if($bounds.Right-$bounds.Left -ne $resolution[1] -or $bounds.Bottom-$bounds.Top -ne $resolution[2]){throw 'Independent application resolution was not applied'}
  ClickScreen ([int](150*800/$resolution[1])) ([int](44+80*456/$resolution[2]))
  [void](Send $pet 0x100 35);[void](Send $pet 0x101 35);[void](Send $pet 0x102 82);$expected+='R'
  Await {HasInput $expected} 'Input mapping failed at higher application resolution'
 }
 [void][EmbeddedWin]::SendMessage($hostWindow,0x801f,[IntPtr]0,[IntPtr]15)
 $second=Start-Process -FilePath $fixture -PassThru
 Await {$script:secondWindow=[AppCheck]::Find($second.Id);$secondWindow -ne [IntPtr]::Zero} 'Second app missing'
 $secondBefore=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($secondWindow,[ref]$secondBefore);$secondStyle=[AppCheck]::GetWindowLongPtr($secondWindow,-16)
 [void][EmbeddedWin]::SendMessage($pet,0x8003,[IntPtr]81,$secondWindow)
 Await {(Send $pet 0x8003 82) -eq 2} 'Multiple app attachment failed'
 [void](Send $pet 0x111 327)
 [void](Send $pet 0x111 325)
 Start-Sleep -Milliseconds 300
 [void][AppCheck]::ShowWindow($appWindow,3)
 Start-Sleep -Milliseconds 600
 $bounds=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($appWindow,[ref]$bounds)
 if($bounds.Right-$bounds.Left -gt 800 -or $bounds.Bottom-$bounds.Top -gt 456){throw 'Maximize escaped the application container'}
 [void](Send $pet 0x111 323)
 Await {[AppCheck]::GetParent($appWindow) -eq [IntPtr]::Zero} 'Original window parent not restored'
 $after=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($appWindow,[ref]$after)
 if(([AppCheck]::GetWindowLongPtr($appWindow,-16).ToInt64() -band 0x40000000) -ne 0){throw 'Restored window is still a child'}
 if($after.Left -lt 0 -or $after.Top -lt 0 -or $after.Right-$after.Left -lt 500){throw 'Window was not restored to the desktop'}
 Await {[AppCheck]::GetParent($secondWindow) -eq [IntPtr]::Zero} 'Second app not restored'
 $secondAfter=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($secondWindow,[ref]$secondAfter)
 if($secondBefore.Left -ne $secondAfter.Left -or $secondBefore.Top -ne $secondAfter.Top -or $secondBefore.Right -ne $secondAfter.Right -or $secondBefore.Bottom -ne $secondAfter.Bottom -or [AppCheck]::GetWindowLongPtr($secondWindow,-16) -ne $secondStyle){throw 'Window placement or style not restored exactly'}
 [void](Send $pet 0x111 320)
 Await {(Send $pet 0x8003 80) -ne 0} 'Second host did not start'
 [void][EmbeddedWin]::SendMessage($pet,0x8003,[IntPtr]81,$appWindow)
 Await {(Send $pet 0x8003 82) -eq 1} 'Reattachment failed'
 [uint32]$petId=0;[void][PicoControl]::GetWindowThreadProcessId($pet,[ref]$petId)
 Stop-Process -Id $petId
 $pet=[IntPtr]::Zero
 Await {[AppCheck]::GetParent($appWindow) -eq [IntPtr]::Zero} 'Helper failed to restore after owner exit'
 @{shortcutArguments=$true;automaticAttachment=$true;independentResolution=$true;scaledInput=$true;capture=$true;projectedInput=$true;buttonClick=$true;multipleWindows=$true;twoModes=$true;containedMaximize=$true;restored=$true;crashRecovery=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/app-workspace-checks.json')
 Get-Content (Join-Path $root 'output/app-workspace-checks.json')
 if($Browser){
  $browserPath=Join-Path ${env:ProgramFiles(x86)} 'Microsoft/Edge/Application/msedge.exe'
  if(!(Test-Path -LiteralPath $browserPath)){throw 'Edge is required for the optional browser regression'}
  $shortcutStore=Join-Path $env:LOCALAPPDATA 'PicoPet/shortcuts.ini'
  $savedShortcuts=if(Test-Path -LiteralPath $shortcutStore){[IO.File]::ReadAllBytes($shortcutStore)}else{$null}
  $browserWindow=[IntPtr]::Zero
  try{
   [IO.File]::WriteAllText($shortcutStore,"[Shortcuts]`r`ncount=1`r`nitem0=$browserPath`r`n",[Text.Encoding]::Unicode)
   [void][AppCheck]::WritePrivateProfileString('PICO','appResolution','3',$config)
   Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
   Await {$script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO');$pet -ne [IntPtr]::Zero} 'Pet restart failed'
   [void](Send $pet 0x111 210);[void](Send $pet 0x111 250);[void](Send $pet 0x111 106);[void](Send $pet 0x111 324)
   [void][AppCheck]::SetWindowPos($pet,[IntPtr](-1),900,220,0,0,0x11)
   $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r);$extent=$r.Right-$r.Left
   $cx=[int]($extent*(.5+(120/800.0-.5)*83.6/120));$cy=[int]($extent*(.5+(44-(42.5+(.5-300/500.0)*47.6))/120))
   [void][EmbeddedWin]::SetCursorPos(($r.Left+$cx),($r.Top+$cy));[void](Send $pet 0x200)
   Start-Sleep -Milliseconds 300
   $existing=Get-Process msedge -ErrorAction SilentlyContinue | Where-Object MainWindowHandle -ne 0 | Select-Object -ExpandProperty MainWindowHandle
   ClickScreen 120 300
   Await {(Send $pet 0x8003 82) -eq 1} 'Normal shortcut click did not automatically attach Edge'
   $hostWindow=[IntPtr](Send $pet 0x8003 80);$browserWindow=[AppCheck]::Next($hostWindow,[IntPtr]::Zero)
   if($browserWindow -eq [IntPtr]::Zero -or $existing -contains $browserWindow){throw 'Browser did not create a separate TV window'}
   foreach($old in $existing){if([AppCheck]::GetParent($old) -ne [IntPtr]::Zero){throw 'Existing user browser window was unexpectedly attached'}}
   Await {(Send $pet 0x8003 83) -gt 3} 'Browser capture missing'
   $autoWidth=[Math]::Min(1920,[Math]::Max(1280,[int][Math]::Round(1024*[AppCheck]::GetDpiForWindow($hostWindow)/96)))
   $autoHeight=[int][Math]::Round($autoWidth*456/800)
   $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($browserWindow,[ref]$r)
   if($r.Right-$r.Left -ne $autoWidth -or $r.Bottom-$r.Top -ne $autoHeight){throw 'Automatic browser canvas did not compensate for DPI'}
   Start-Sleep -Milliseconds 700
   $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
   $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top);$graphics=[Drawing.Graphics]::FromImage($bitmap)
   try{$graphics.CopyFromScreen($r.Left,$r.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $root 'output/browser-auto-fit.png'))}finally{$graphics.Dispose();$bitmap.Dispose()}
   [void](Send $pet 0x111 328)
   Await {$c=[AppCheck]::Content($browserWindow);[Math]::Abs(($c.Right-$c.Left)-$autoWidth) -le 2 -and [Math]::Abs(($c.Bottom-$c.Top)-$autoHeight) -le 2} 'Fullscreen browser viewport does not match TV aspect and resolution'
   [void](Send $pet 0x111 328)
   [void](Send $pet 0x111 300)
   Await {$script:prefs=[EmbeddedWin]::FindWindow('PicoPet.Preferences','PICO · 偏好设置');$prefs -ne [IntPtr]::Zero} 'Preferences missing'
   foreach($resolution in @(@(1,1280,730),@(2,1600,912))){
    [void](Send ([EmbeddedWin]::GetDlgItem($prefs,1020)) 0x14e ($resolution[0]+1));[void](Send $prefs 0x111 (1020 -bor (1 -shl 16)))
    Await {$r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($browserWindow,[ref]$r);($r.Right-$r.Left -eq $resolution[1]) -and ($r.Bottom-$r.Top -eq $resolution[2])} 'Browser resolution did not update from settings'
   }
   [void](Send ([EmbeddedWin]::GetDlgItem($prefs,1019)) 0x14e 1);[void](Send $prefs 0x111 (1019 -bor (1 -shl 16)))
   if(!((Get-Content -LiteralPath $config) -contains 'shortcutTarget=1') -or !((Get-Content -LiteralPath $config) -contains 'appResolution=2')){throw 'Application preferences were not persisted'}
   [void](Send $prefs 0x10)
   @{normalShortcutClick=$true;automaticBrowserAttachment=$true;automaticDpiCanvas=$true;fullscreenViewportFits=$true;existingWindowsPreserved=$true;resolutionSettings=$true;preferencesSaved=$true;processId=[AppCheck]::ProcessId($browserWindow)} | ConvertTo-Json | Set-Content (Join-Path $root 'output/browser-workspace-checks.json')
   Get-Content (Join-Path $root 'output/browser-workspace-checks.json')
  }finally{
   if($pet -ne [IntPtr]::Zero){[void](Send $pet 0x111 323)}
   if($browserWindow -ne [IntPtr]::Zero){[void][AppCheck]::PostMessage($browserWindow,0x10,[IntPtr]0,[IntPtr]0)}
   & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit;$pet=[IntPtr]::Zero
   if($null -ne $savedShortcuts){[IO.File]::WriteAllBytes($shortcutStore,$savedShortcuts)}else{Remove-Item -LiteralPath $shortcutStore -ErrorAction SilentlyContinue}
  }
 }
} finally {
 if($pet -ne [IntPtr]::Zero){[void](Send $pet 0x111 323)}
 if($app -and !$app.HasExited){[void](Send ([AppCheck]::Find($app.Id)) 0x10)}
 if($second -and !$second.HasExited){[void](Send ([AppCheck]::Find($second.Id)) 0x10)}
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 Copy-Item -LiteralPath $backup -Destination $config -Force
 Start-Process -FilePath $restart -WindowStyle Hidden
 [void][EmbeddedWin]::SetThreadDpiAwarenessContext($oldDpi)
}
