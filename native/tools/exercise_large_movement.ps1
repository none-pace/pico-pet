$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class MovementCheck {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
}
'@
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini'
$saved=@{};foreach($line in Get-Content $config){if($line -match '^(size|view|yaw|pitch|x|y|floating|paused)=(.*)$'){$saved[$matches[1]]=$matches[2]}}
$oldDpi=[EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$cursor=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
function Rect {$r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r);return $r}
function Drag([int]$dx,[int]$dy){
 $r=Rect;$size=$r.Right-$r.Left;$x=$r.Left+[int]($size*.5);$y=$r.Top+[int]($size*.5)
 [void][EmbeddedWin]::SetCursorPos($x,$y);[void](Send $pet 0x200);[void](Send $pet 0x201)
 for($i=1;$i -le 20;$i++){[void][EmbeddedWin]::SetCursorPos(($x+[int]($dx*$i/20)),($y+[int]($dy*$i/20)));[void](Send $pet 0x200);Start-Sleep -Milliseconds 20}
 [void](Send $pet 0x202);Start-Sleep -Milliseconds 220
}
try {
 if((Send $pet 0x8003 2) -band 4){[void](Send $pet 0x111 101)}
 if((Send $pet 0x8003 4) -band 1){[void](Send $pet 0x111 108)}
 [void](Send $pet 0x111 210)
 [void](Send $pet 0x111 300);$preferences=[EmbeddedWin]::FindWindow('PicoPet.Preferences','PICO · 偏好设置')
 [void][EmbeddedWin]::SendText([EmbeddedWin]::GetDlgItem($preferences,1000),0xC,[IntPtr]::Zero,'1024');Start-Sleep -Milliseconds 650;[void](Send $preferences 0x10)
 [void][EmbeddedWin]::SetCursorPos(1000,500)
 [void](Send $pet 0x111 106);Start-Sleep -Milliseconds 100
 $bottom=Rect
 Drag 0 -440;$top=Rect
 if($bottom.Top-$top.Top -lt 100){throw "Large model vertical travel is still constrained: $($bottom.Top-$top.Top) px"}
 Drag 0 440;$returned=Rect
 if($returned.Top-$top.Top -lt 100){throw 'Large model cannot drag down after dragging up'}
 Drag -350 0;$left=Rect
 if($returned.Left-$left.Left -lt 100){throw 'Horizontal dragging regressed'}
 [void](Send $pet 0x111 210);Start-Sleep -Milliseconds 100
 $r=Rect;[void][EmbeddedWin]::SetCursorPos(($r.Left+[int](($r.Right-$r.Left)*.5)),($r.Top+[int](($r.Bottom-$r.Top)*.5)))
 [void](Send $pet 0x200);[void](Send $pet 0x8003 41)
 Copy-Item -LiteralPath (Join-Path $env:TEMP 'pico-render.bmp') -Destination (Join-Path $root 'output/large-model-movement.bmp') -Force
 @{maximumSize=1024;windowExtent=$bottom.Right-$bottom.Left;verticalTravel=$bottom.Top-$top.Top;downwardTravel=$returned.Top-$top.Top;horizontalTravel=$returned.Left-$left.Left;upperWindowTop=$top.Top;lowerWindowTop=$bottom.Top}|ConvertTo-Json|Set-Content (Join-Path $root 'output/large-movement-test.json')
 Get-Content (Join-Path $root 'output/large-movement-test.json')
} finally {
 [void](Send $pet 0x202)
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 foreach($entry in $saved.GetEnumerator()){[void][MovementCheck]::WritePrivateProfileString('PICO',$entry.Key,$entry.Value,$config)}
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 [void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y);[void][EmbeddedWin]::SetThreadDpiAwarenessContext($oldDpi)
}
