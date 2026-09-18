$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class DesktopCheck {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$root=Split-Path $PSScriptRoot -Parent
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini'
$saved=@{}
foreach($line in Get-Content $config){if($line -match '^(size|view|yaw|pitch|x|y|hd)=(.*)$'){$saved[$matches[1]]=$matches[2]}}
$oldDpi=[EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$cursor=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
function MoveScreen([int]$x,[int]$y){
 $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r);$extent=$r.Right-$r.Left
 $worldX=($x/800.0-.5)*83.6;$worldY=42.5+(.5-$y/500.0)*47.6
 [void][EmbeddedWin]::SetCursorPos(($r.Left+[int]($extent*(.5+$worldX/120))),($r.Top+[int]($extent*(.5+(44-$worldY)/120))))
 [void](Send $pet 0x200);Start-Sleep -Milliseconds 160
}
function CaptureDesktop([string]$name){
 $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
 $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top);$g=[Drawing.Graphics]::FromImage($bitmap)
 try{$g.CopyFromScreen($r.Left,$r.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $root "output/$name.png"))}finally{$g.Dispose();$bitmap.Dispose()}
}
try {
 if($pet -eq [IntPtr]::Zero){throw 'PICO is not running'}
 [void](Send $pet 0x111 210);[void](Send $pet 0x111 250);[void](Send $pet 0x111 261)
 foreach($sample in @(@(118,145,0),@(306,145,1),@(494,145,2),@(682,145,3),@(400,35,-2),@(400,408,-2),@(64,465,1003),@(735,465,1000))){
  MoveScreen $sample[0] $sample[1]
  $actual=Send $pet 0x8003 16
  if($actual -ne $sample[2]){throw "Wrong desktop hit at $($sample[0]),$($sample[1]): expected $($sample[2]), got $actual"}
  if((Send $pet 0x8003 14) -ne 1){throw 'Desktop disappeared inside glass'}
 }
 MoveScreen 306 145;CaptureDesktop 'desktop-front-hover'
 [void](Send $pet 0x201);CaptureDesktop 'desktop-pressed';[void](Send $pet 0x202)
 Start-Sleep -Milliseconds 300
 $desk=[EmbeddedWin]::FindWindow('PicoPet.SystemDesk','PICO 系统 · 本机状态')
 if($desk -eq [IntPtr]::Zero){throw 'Screen module did not open'}
 if((Send ([EmbeddedWin]::GetDlgItem($desk,3000)) 0x130B) -ne 1){throw 'Wrong module opened'}
 [void](Send $desk 0x10)
 MoveScreen 64 465;[void](Send $pet 0x201);[void](Send $pet 0x202);Start-Sleep -Milliseconds 600
 if((Send $pet 0x8003 25) -ne 1){throw 'Screen terminal dock did not open embedded console'}
 TypeText 'echo PICO_TV_CMD_OK';[void](Send $pet 0x102 13);Start-Sleep -Milliseconds 700
 if((Send $pet 0x8003 28) -ne 1){throw 'Desktop texture broke embedded CMD output'}
 [void](Send $pet 0x111 244);MoveScreen 118 145;CaptureDesktop 'desktop-after-console'
 $frames=Send $pet 0x8003 0;Start-Sleep -Milliseconds 650
 if((Send $pet 0x8003 0)-$frames -gt 2){throw 'Stationary hover is continuously repainting'}
 [void](Send $pet 0x111 211);Start-Sleep -Milliseconds 250
 $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r);$extent=$r.Right-$r.Left
 $found=$false
 for($y=.36;$y -lt .62 -and !$found;$y+=.025){for($x=.3;$x -lt .6 -and !$found;$x+=.025){[void][EmbeddedWin]::SetCursorPos(($r.Left+[int]($extent*$x)),($r.Top+[int]($extent*$y)));[void](Send $pet 0x200);if((Send $pet 0x8003 16) -eq 2){$found=$true}}}
 if(!$found){throw 'Rotated 3D desktop icon is not hittable'}
 Start-Sleep -Milliseconds 180;CaptureDesktop 'desktop-3d-hover'
 [void](Send $pet 0x111 260);Start-Sleep -Milliseconds 150;CaptureDesktop 'desktop-pixel'
 [void][EmbeddedWin]::SetCursorPos(5,5);[void](Send $pet 0x200);Start-Sleep -Milliseconds 200
 if((Send $pet 0x8003 14) -ne 0 -or (Send $pet 0x8003 9) -ge 7){throw 'Leaving glass did not restore the pet face'}
 @{iconBounds=$true;moduleLaunch=$true;embeddedTerminal=$true;textureRestored=$true;stationaryHoverSleeps=$true;rotatedHit=$true;faceRestored=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/screen-desktop-test.json')
 Get-Content (Join-Path $root 'output/screen-desktop-test.json')
} finally {
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 foreach($entry in $saved.GetEnumerator()){[void][DesktopCheck]::WritePrivateProfileString('PICO',$entry.Key,$entry.Value,$config)}
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 [void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y);[void][EmbeddedWin]::SetThreadDpiAwarenessContext($oldDpi)
}
