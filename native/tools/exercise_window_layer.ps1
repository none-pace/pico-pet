param([switch]$Fixture,[string]$Instance='Application')
$ErrorActionPreference='Stop'
if($Fixture){
 Add-Type -AssemblyName System.Windows.Forms
 $form=New-Object Windows.Forms.Form;$form.Text="PICO Layer Test $Instance";$form.Width=600;$form.Height=400
 $second=New-Object Windows.Forms.Form;$second.Text='PICO Layer Test Second';$second.Width=360;$second.Height=240
 $form.KeyPreview=$true;$form.add_KeyDown({param($sender,$eventArgs)if($eventArgs.KeyCode -eq 'F8'){$second.Show()}})
 $tool=New-Object Windows.Forms.Form;$tool.Text='PICO Layer Test Tool';$tool.ShowInTaskbar=$false;$tool.FormBorderStyle='FixedToolWindow'
 $form.add_Shown({$tool.Show($form)})
 $form.add_Paint({param($sender,$eventArgs)
   $g=$eventArgs.Graphics
   $pen=New-Object Drawing.Pen ([Drawing.Color]::FromArgb(40,90,180)),4
   $brush=New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(230,240,255))
   $font=New-Object Drawing.Font 'Segoe UI',22
   $rowFont=New-Object Drawing.Font 'Consolas',14
   $markerBrush=New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(210,60,60))
   $textBrush=New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(20,40,80))
   try {
   $g.Clear([Drawing.Color]::White)
   $g.FillRectangle($brush,30,30,540,120)
   $g.DrawRectangle($pen,30,30,540,120)
   $g.DrawLine($pen,30,200,570,200)
   $g.FillEllipse($markerBrush,60,240,90,90)
   $g.DrawString('PICO LAYER TEST',$font,$textBrush,50,60)
   $g.DrawString('row 1  ------------------------------',$rowFont,[Drawing.Brushes]::Black,50,220)
   $g.DrawString('row 2  ------------------------------',$rowFont,[Drawing.Brushes]::Black,50,250)
   $g.DrawString('row 3  ------------------------------',$rowFont,[Drawing.Brushes]::Black,50,280)
   } finally {$pen.Dispose();$brush.Dispose();$font.Dispose();$rowFont.Dispose();$markerBrush.Dispose();$textBrush.Dispose()}
 })
 [Windows.Forms.Application]::Run($form);$second.Dispose();$tool.Dispose();$form.Dispose();exit
}
Add-Type @'
using System;using System.Text;using System.Runtime.InteropServices;
public static class LayerCheck {
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [StructLayout(LayoutKind.Sequential)]public struct POINT{public int X,Y;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 public static IntPtr FindTitle(string title){return FindWindow(null,title);}
 public static IntPtr FindClass(string c){return FindWindow(c,null);}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,StringBuilder l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern IntPtr GetWindow(IntPtr h,uint kind);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern IntPtr GetWindowLongPtr(IntPtr h,int index);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int cmd);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int z,uint f);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out POINT p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern void mouse_event(uint f,uint x,uint y,uint d,UIntPtr e);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
}
'@
function Send($h,[uint32]$m,[long]$w=0,[long]$l=0){[LayerCheck]::SendMessage($h,$m,[IntPtr]$w,[IntPtr]$l).ToInt64()}
function ChooseOption($parent,[int]$id,[int]$index){$child=[LayerCheck]::GetDlgItem($parent,$id);[void](Send $child 0x14e $index);[void](Send $parent 0x111 ($id -bor (1 -shl 16)) $child.ToInt64())}
function Await([scriptblock]$condition,[string]$message){for($i=0;$i -lt 60;$i++){if(& $condition){return};Start-Sleep -Milliseconds 100};throw $message}
function PetWindow {[LayerCheck]::FindWindow('PicoPet.Win11.Native','PICO')}
function Below($lower,$upper){for($w=[LayerCheck]::GetWindow($upper,2);$w -ne [IntPtr]::Zero;$w=[LayerCheck]::GetWindow($w,2)){if($w -eq $lower){return $true}};return $false}
$root=Split-Path $PSScriptRoot -Parent;$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini';$saved=@{}
foreach($line in Get-Content $config){if($line -match '^(layerMode|layerPid|layerPath|topmost)=(.*)$'){$saved[$matches[1]]=$matches[2]}}
$oldDpi=[LayerCheck]::SetThreadDpiAwarenessContext([IntPtr](-4));$cursor=New-Object LayerCheck+POINT;[void][LayerCheck]::GetCursorPos([ref]$cursor)
$fixtureProcess=$null;$otherProcess=$null;$preferences=[IntPtr]::Zero
try {
 Await {(PetWindow) -ne [IntPtr]::Zero} 'Pet is not running'
 $fixtureProcess=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$PSCommandPath`"",'-Fixture' -WindowStyle Hidden -PassThru
 Await {[LayerCheck]::FindTitle('PICO Layer Test Application') -ne [IntPtr]::Zero} 'Fixture did not start'
 Start-Sleep -Milliseconds 500
 $target=[LayerCheck]::FindTitle('PICO Layer Test Application');$pet=PetWindow
 [void][LayerCheck]::ShowWindow($target,5);Start-Sleep -Milliseconds 250
 [void](Send $pet 0x111 300)
 $preferences=[IntPtr]::Zero
 for($i=0;$i -lt 30;$i++){$preferences=[LayerCheck]::FindClass('PicoPet.Preferences');if($preferences -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
 if($preferences -eq [IntPtr]::Zero){throw 'Preferences missing'}
 $tabs=[LayerCheck]::GetDlgItem($preferences,903);$r=New-Object LayerCheck+RECT;[void][LayerCheck]::GetWindowRect($tabs,[ref]$r)
 [void][LayerCheck]::SetCursorPos(($r.Left+180),($r.Top+15));[LayerCheck]::mouse_event(2,0,0,0,[UIntPtr]::Zero);Start-Sleep -Milliseconds 60;[LayerCheck]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
 ChooseOption $preferences 910 1
 $list=[LayerCheck]::GetDlgItem($preferences,911);$selected=-1;$label=''
 $labels=@();for($i=1;$i -lt (Send $list 0x146);$i++){$text=New-Object Text.StringBuilder 2048;[void][LayerCheck]::ReadText($list,0x148,[IntPtr]$i,$text);$labels+=$text.ToString();if($text.ToString().Contains("PID $($fixtureProcess.Id) ")){$selected=$i;$label=$text.ToString();break}}
 if($selected -lt 1 -or $label.Contains('PICO Layer Test Tool') -or !$label.Contains('1 个窗口')){throw "Taskbar GUI filter failed for PID $($fixtureProcess.Id): $($labels -join '; ')"}
 ChooseOption $preferences 911 $selected
 if((Send $tabs 0x130b) -ne 1){throw 'Window and layer category was not selected'}
 Start-Sleep -Milliseconds 350
 Add-Type -AssemblyName System.Drawing
 $capture=New-Object LayerCheck+RECT;[void][LayerCheck]::GetWindowRect($preferences,[ref]$capture)
 $bitmap=New-Object Drawing.Bitmap ($capture.Right-$capture.Left),($capture.Bottom-$capture.Top);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($capture.Left,$capture.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $root 'output/window-layer-preferences.png'))}finally{$graphics.Dispose();$bitmap.Dispose()}
 [void](Send $preferences 0x10);$preferences=[IntPtr]::Zero
 Await {(Send $pet 0x8003 63) -eq $target.ToInt64() -and (Below $pet $target)} 'Pet did not move below selected process'
 [void][LayerCheck]::SetWindowPos($target,[IntPtr](-1),0,0,0,0,0x13)
 try{Await {(Below $pet $target) -and (([LayerCheck]::GetWindowLongPtr($pet,-20).ToInt64() -band 8) -ne 0)} 'Topmost target band was not followed'}catch{throw "$_ targetStyle=$([LayerCheck]::GetWindowLongPtr($target,-20)) petStyle=$([LayerCheck]::GetWindowLongPtr($pet,-20)) anchor=$(Send $pet 0x8003 63) below=$(Below $pet $target)"}
 [void][LayerCheck]::SetWindowPos($target,[IntPtr](-2),0,0,0,0,0x13)
 Await {(Below $pet $target) -and (([LayerCheck]::GetWindowLongPtr($pet,-20).ToInt64() -band 8) -eq 0)} 'Normal target band was not restored'
 [void][LayerCheck]::ShowWindow($target,6)
 Await {(Send $pet 0x8003 63) -eq 0 -and (([LayerCheck]::GetWindowLongPtr($pet,-20).ToInt64() -band 8) -eq 0)} 'Minimized target fallback failed'
 [void][LayerCheck]::ShowWindow($target,9)
 Await {(Send $pet 0x8003 63) -eq $target.ToInt64() -and (Below $pet $target)} 'Restored target did not reattach'
 [void](Send $target 0x100 0x77)
 Await {[LayerCheck]::FindTitle('PICO Layer Test Second') -ne [IntPtr]::Zero} 'Second GUI window did not open'
 $second=[LayerCheck]::FindTitle('PICO Layer Test Second')
 Await {(Below $pet $target) -and (Below $pet $second)} 'Pet must stay below every taskbar window of the selected process'
 [void](Send $second 0x10)
 $otherProcess=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$PSCommandPath`"",'-Fixture','-Instance','Other' -WindowStyle Hidden -PassThru
 Await {[LayerCheck]::FindTitle('PICO Layer Test Other') -ne [IntPtr]::Zero} 'Same-executable second process did not start'
 $other=[LayerCheck]::FindTitle('PICO Layer Test Other');[void][LayerCheck]::ShowWindow($other,5)
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 Await {(PetWindow) -ne [IntPtr]::Zero} 'Pet did not restart';$pet=PetWindow
 Await {(Send $pet 0x8003 62) -eq 1 -and (Send $pet 0x8003 63) -eq $target.ToInt64() -and (Below $pet $target)} 'Layer preference did not persist after restart'
 if((Get-Content $config -Raw) -notmatch "(?m)^layerPid=$($fixtureProcess.Id)\s*$"){throw 'Selected PID was not saved'}
 [void][LayerCheck]::ShowWindow($target,6)
 Await {(Send $pet 0x8003 63) -eq 0} 'Minimizing selected process switched to another instance'
 [void][LayerCheck]::ShowWindow($target,9)
 Await {(Send $pet 0x8003 63) -eq $target.ToInt64()} 'Original instance did not recover'
 [void][LayerCheck]::PostMessage($other,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
 if(!$otherProcess.WaitForExit(5000)){throw 'Second fixture did not exit'}
 [void][LayerCheck]::PostMessage($target,0x10,[IntPtr]::Zero,[IntPtr]::Zero);$fixtureProcess.WaitForExit(5000)|Out-Null
 Await {(Send $pet 0x8003 63) -eq 0} 'Closed target did not detach'
 $fixtureProcess=Start-Process -FilePath (Get-Process -Id $PID).Path -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',"`"$PSCommandPath`"",'-Fixture' -WindowStyle Hidden -PassThru
 Await {[LayerCheck]::FindTitle('PICO Layer Test Application') -ne [IntPtr]::Zero} 'Restarted fixture did not start'
 $target=[LayerCheck]::FindTitle('PICO Layer Test Application');[void][LayerCheck]::ShowWindow($target,5)
 Await {(Send $pet 0x8003 63) -eq $target.ToInt64() -and (Below $pet $target)} 'Restarted process did not reattach by verified path'
 [void](Send $pet 0x111 300);$preferences=[IntPtr]::Zero
 for($i=0;$i -lt 30;$i++){$preferences=[LayerCheck]::FindClass('PicoPet.Preferences');if($preferences -ne [IntPtr]::Zero){break};Start-Sleep -Milliseconds 100}
 $list=[LayerCheck]::GetDlgItem($preferences,911)
 $chosen=Send $list 0x147;$text=New-Object Text.StringBuilder 2048;[void][LayerCheck]::ReadText($list,0x148,[IntPtr]$chosen,$text)
 if(!$text.ToString().Contains("PID $($fixtureProcess.Id) ")){throw 'Preferences retained an obsolete process selection'}
 [void](Send $preferences 0x10);$preferences=[IntPtr]::Zero
 $idleFrames=$null
 if(!((Send $pet 0x8003 4) -band 1)){Start-Sleep -Milliseconds 350;$before=Send $pet 0x8003 0;Start-Sleep -Milliseconds 1300;$idleFrames=(Send $pet 0x8003 0)-$before;if($idleFrames -gt 2){throw "Layer maintenance caused idle rendering: $idleFrames frames"}}
 @{taskbarGuiOnly=$true;normalAndTopmost=$true;minimizeRestore=$true;multipleWindows=$true;restartPersistence=$true;sameExecutableIsolation=$true;closedTargetFallback=$true;processRestart=$true;selectionRebind=$true;idleFrames=$idleFrames;selected=$label}|ConvertTo-Json|Set-Content (Join-Path $root 'output/window-layer-test.json')
 Get-Content (Join-Path $root 'output/window-layer-test.json')
}finally{
 if($preferences -ne [IntPtr]::Zero){[void](Send $preferences 0x10)}
 $target=[LayerCheck]::FindTitle('PICO Layer Test Application');if($target -ne [IntPtr]::Zero){[void][LayerCheck]::PostMessage($target,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
 $other=[LayerCheck]::FindTitle('PICO Layer Test Other');if($other -ne [IntPtr]::Zero){[void][LayerCheck]::PostMessage($other,0x10,[IntPtr]::Zero,[IntPtr]::Zero)}
 foreach($process in @($fixtureProcess,$otherProcess)){if($process -and !$process.HasExited){[void]$process.WaitForExit(5000)}}
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 foreach($key in @('layerMode','layerPid','layerPath','topmost')){[void][LayerCheck]::WritePrivateProfileString('PICO',$key,$saved[$key],$config)}
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 [void][LayerCheck]::SetCursorPos($cursor.X,$cursor.Y);[void][LayerCheck]::SetThreadDpiAwarenessContext($oldDpi)
}
