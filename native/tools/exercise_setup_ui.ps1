$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class SetupUi {
 public delegate bool Callback(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,Callback cb,IntPtr p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetWindowText(IntPtr h,StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 public static string Text(IntPtr window){var result=new StringBuilder();EnumChildWindows(window,(h,p)=>{var s=new StringBuilder(4096);GetWindowText(h,s,4096);result.AppendLine(s.ToString());return true;},IntPtr.Zero);return result.ToString();}
}
'@
function WaitDialog([string]$needle){
 for($i=0;$i -lt 300;$i++){
  $dialog=[SetupUi]::FindWindow('#32770','PICO 安装程序')
  if($dialog -ne [IntPtr]::Zero -and [SetupUi]::Text($dialog).Contains($needle)){return $dialog}
  Start-Sleep -Milliseconds 100
 }
 throw "Installer dialog missing: $needle"
}
function Capture([IntPtr]$dialog,[string]$name){
 [void][SetupUi]::SetWindowPos($dialog,[IntPtr](-1),0,0,0,0,0x13)
 Start-Sleep -Milliseconds 150
 $rect=New-Object SetupUi+RECT
 [void][SetupUi]::GetWindowRect($dialog,[ref]$rect)
 $bitmap=New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
 $graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($rect.Left,$rect.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $root "output/$name.png"))}
 finally{$graphics.Dispose();$bitmap.Dispose();[void][SetupUi]::SetWindowPos($dialog,[IntPtr](-2),0,0,0,0,0x13)}
}
$oldDpi=[SetupUi]::SetThreadDpiAwarenessContext([IntPtr](-4))
try {
 $setup=Start-Process -FilePath (Join-Path $root 'dist/PICO-Setup.exe') -WindowStyle Hidden -PassThru
 $dialog=WaitDialog '安装 PICO 桌宠到'
 Capture $dialog 'setup-confirm'
 [void][SetupUi]::PostMessage($dialog,0x111,[IntPtr]1,[IntPtr]::Zero)
 $dialog=WaitDialog '安装完成'
 Capture $dialog 'setup-complete'
 [void][SetupUi]::PostMessage($dialog,0x111,[IntPtr]1,[IntPtr]::Zero)
 if(!$setup.WaitForExit(20000) -or $setup.ExitCode -ne 0){throw 'Interactive installer did not finish'}
 $pet=Get-Process PicoPet -ErrorAction Stop
 if($pet.Path -ine (Join-Path $env:LOCALAPPDATA 'Programs/PicoPet/PicoPet.exe')){throw 'Interactive install did not start the installed pet'}
 @{interactiveInstall=$true;autoLaunch=$true;installerExited=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/setup-ui-test.json') -Encoding utf8
 Get-Content -LiteralPath (Join-Path $root 'output/setup-ui-test.json')
}finally{[void][SetupUi]::SetThreadDpiAwarenessContext($oldDpi)}
