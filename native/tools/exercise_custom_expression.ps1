$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Monitor
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ExpressionCheck {
 [StructLayout(LayoutKind.Sequential)]public struct Point{public int X,Y;}
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out Point p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
}
'@
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini'
$imageFile=Join-Path $env:LOCALAPPDATA 'PicoPet/expression.png'
$imageConfig=Join-Path $env:LOCALAPPDATA 'PicoPet/expression.ini'
if((Test-Path $imageFile) -or (Test-Path $imageConfig)){throw 'Existing custom expression must be preserved; use an isolated profile for this test'}
$saved=@{};foreach($line in Get-Content $config){if($line -match '^(size|view|yaw|pitch|x|y|hd|mood)=(.*)$'){$saved[$matches[1]]=$matches[2]}}
$cursor=New-Object ExpressionCheck+Point;[void][ExpressionCheck]::GetCursorPos([ref]$cursor)
$fixture=Join-Path $root "output/透明立绘测试-$PID.png"
$bitmap=New-Object Drawing.Bitmap 240,480;$graphics=[Drawing.Graphics]::FromImage($bitmap)
try{
 $graphics.Clear([Drawing.Color]::Transparent)
 $graphics.FillRectangle([Drawing.Brushes]::SeaGreen,40,20,160,140)
 $graphics.FillRectangle([Drawing.Brushes]::CornflowerBlue,40,170,160,140)
 $graphics.FillRectangle([Drawing.Brushes]::Coral,40,320,160,140)
 $bitmap.Save($fixture,[Drawing.Imaging.ImageFormat]::Png)
}finally{$graphics.Dispose();$bitmap.Dispose()}
function Away {[void][ExpressionCheck]::SetCursorPos(5,5);[void](Message $pet 0x200);Start-Sleep -Milliseconds 150}
function FrameHash {
 [void](Message $pet 0x8003 41)
 (Get-FileHash (Join-Path $env:TEMP 'pico-render.bmp')).Hash
}
try {
 [void](Message $pet 0x111 210);[void](Message $pet 0x111 250);[void](Message $pet 0x111 261);[void](Message $pet 0x111 222);Away
 $builtin=FrameHash
 [void][MonitorWin]::PostMessage($pet,0x111,[IntPtr]310,[IntPtr]::Zero)
 $dialog=[IntPtr]::Zero
 for($i=0;$i -lt 50;$i++){$dialog=[MonitorWin]::FindWindow('#32770','选择自定义表情图片');if($dialog -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
 if($dialog -eq [IntPtr]::Zero){throw 'Image picker did not open'}
 Start-Sleep -Milliseconds 900;$edit=[MonitorWin]::FindFilename($dialog)
 if($edit -eq [IntPtr]::Zero){throw 'Image picker filename control missing'}
 [void][MonitorWin]::SendText($edit,0xC,[IntPtr]::Zero,$fixture);Start-Sleep -Milliseconds 300
 [void][MonitorWin]::PostMessage([MonitorWin]::GetDlgItem($dialog,1),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)
 for($i=0;$i -lt 60;$i++){if(Test-Path $imageConfig){break};Start-Sleep -Milliseconds 100}
 if(!(Test-Path $imageFile) -or (Get-Content $imageConfig -Raw) -notmatch 'enabled=1'){throw 'Image import did not persist'}
 Away;$custom=FrameHash;if($custom -eq $builtin){throw 'Imported image did not change screen pixels'}
 $script:desk=$pet;Capture 'expression-portrait-contain'
 $frames=Message $pet 0x8003 0;Start-Sleep -Milliseconds 1500
 if((Message $pet 0x8003 0) -ne $frames){throw 'Custom static image continuously repaints'}
 [void](Message $pet 0x111 314);Away;if((FrameHash) -eq $custom){throw 'Cover setting did not update screen'}
 Capture 'expression-cover'
 [void](Message $pet 0x111 313);[void](Message $pet 0x111 316);Away;$light=FrameHash
 if($light -eq $custom){throw 'Background choice had no effect'}
 Capture 'expression-light-background'
 [void](Message $pet 0x111 244);Start-Sleep -Milliseconds 400
 if((Message $pet 0x8003 25) -ne 1 -or (FrameHash) -eq $light){throw 'Custom image obscured the terminal'}
 [void](Message $pet 0x111 244);Away;if((FrameHash) -ne $light){throw 'Closing terminal did not restore imported image'}
 $r=New-Object MonitorWin+RECT;[void][MonitorWin]::GetWindowRect($pet,[ref]$r);$extent=$r.Right-$r.Left
 [void][ExpressionCheck]::SetCursorPos(($r.Left+[int]($extent*.5)),($r.Top+[int]($extent*.45)));[void](Message $pet 0x200);Start-Sleep -Milliseconds 200
 if((Message $pet 0x8003 14) -ne 1 -or (FrameHash) -eq $light){throw 'Custom expression blocked the desktop'}
 Away;if((FrameHash) -ne $light){throw 'Leaving desktop did not restore imported image'}
 Remove-Item -LiteralPath $fixture
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden;Start-Sleep -Milliseconds 800
 $pet=[MonitorWin]::FindWindow('PicoPet.Win11.Native','PICO');Away
 if((FrameHash) -ne $light){throw 'Image failed to survive restart without its original file'}
 [void](Message $pet 0x111 211);Away;$script:desk=$pet;Capture 'expression-3d'
 [void](Message $pet 0x111 210);[void](Message $pet 0x111 312);Away;if((FrameHash) -ne $builtin){throw 'Restoring built-in expression failed'}
 [void](Message $pet 0x111 311);Away;if((FrameHash) -ne $light){throw 'Re-enabling retained image failed'}
 [void](Message $pet 0x111 300)
 $preferences=[MonitorWin]::FindWindow('PicoPet.Preferences','PICO · 偏好设置')
 if([MonitorWin]::GetDlgItem($preferences,901) -eq [IntPtr]::Zero){throw 'Preferences image import entry missing'}
 [void](Message $preferences 0x10)
 @{picker=$true;transparentPortrait=$true;fitAndCrop=$true;background=$true;terminalAndDesktop=$true;restartWithoutOriginal=$true;restoreBuiltin=$true;idleNoRedraw=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/custom-expression-test.json')
 Get-Content (Join-Path $root 'output/custom-expression-test.json')
} finally {
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 foreach($entry in $saved.GetEnumerator()){[void][ExpressionCheck]::WritePrivateProfileString('PICO',$entry.Key,$entry.Value,$config)}
 foreach($path in @($imageFile,$imageConfig,$fixture)){if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path}}
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 [void][ExpressionCheck]::SetCursorPos($cursor.X,$cursor.Y);[void][MonitorWin]::SetThreadDpiAwarenessContext($oldDpi)
}
