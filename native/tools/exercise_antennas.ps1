$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Runtime.InteropServices;
public static class AntennaCheck {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool WritePrivateProfileString(string s,string k,string v,string p);
}
'@
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini'
$saved=@{};foreach($line in Get-Content $config){if($line -match '^(size|view|yaw|pitch|x|y|floating|paused|hd|mood)=(.*)$'){$saved[$matches[1]]=$matches[2]}}
$oldDpi=[EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$cursor=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
function Rect {$r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r);return $r}
function Drag([int]$dy){
 $r=Rect;$size=$r.Right-$r.Left;$x=$r.Left+[int]($size*.5);$y=$r.Top+[int]($size*.64)
 [void][EmbeddedWin]::SetCursorPos($x,$y);[void](Send $pet 0x200);[void](Send $pet 0x201)
 for($i=1;$i -le 32;$i++){[void][EmbeddedWin]::SetCursorPos($x,($y+[int]($dy*$i/32)));[void](Send $pet 0x200);Start-Sleep -Milliseconds 17}
 [void](Send $pet 0x202)
}
function Capture([string]$name){
 [void](Send $pet 0x8003 41)
 $path=Join-Path $env:TEMP 'pico-render.bmp';$bytes=[IO.File]::ReadAllBytes($path);$r=Rect;$size=$r.Right-$r.Left
 $first=-1;for($y=0;$y -lt $size -and $first -lt 0;$y++){for($x=0;$x -lt $size;$x++){if($bytes[54+($y*$size+$x)*4+3] -gt 20){$first=$y;break}}}
 Copy-Item -LiteralPath $path -Destination (Join-Path $root "output/antenna-$name.bmp") -Force
 return @{left=Send $pet 0x8003 59;right=Send $pet 0x8003 60;recovering=Send $pet 0x8003 61;visibleTop=$r.Top+$first;windowTop=$r.Top}
}
try {
 if($pet -eq [IntPtr]::Zero -or (Send $pet 0x8003 35) -ne 1){throw 'Native model must be running'}
 if((Send $pet 0x8003 2) -band 4){[void](Send $pet 0x111 101)}
 if((Send $pet 0x8003 4) -band 1){[void](Send $pet 0x111 108)}
 [void](Send $pet 0x111 210);[void](Send $pet 0x111 261);[void](Send $pet 0x111 250)
 [void][EmbeddedWin]::SetCursorPos(1000,500);[void](Send $pet 0x111 106);Start-Sleep -Milliseconds 800
 $upright=Capture 'upright'
 Drag (-$upright.visibleTop-25);Start-Sleep -Milliseconds 1100
 $partial=Capture 'partial'
 Drag -600;Start-Sleep -Milliseconds 1100
 $folded=Capture 'folded'
 if($folded.left -lt 400 -or $folded.right -lt 400){throw "Antennas did not fold at top edge: $($folded|ConvertTo-Json -Compress)"}
 if($partial.left -le 0 -or $partial.left -ge $folded.left){throw 'Antenna compression must increase with upward pressure'}
 if($folded.visibleTop -lt 0){throw 'Compressed model crossed the primary work area top'}
 if($folded.recovering){throw 'Stationary contact must not keep the recovery clock running'}
 Drag 300;Start-Sleep -Milliseconds 1100
 [void][EmbeddedWin]::SetCursorPos(1000,500);[void](Send $pet 0x200);Start-Sleep -Milliseconds 200
 $returned=Capture 'returned'
 if($returned.left -ne 0 -or $returned.right -ne 0 -or $returned.recovering){throw 'Antennas did not settle upright after moving away'}
 $before=Send $pet 0x8003 0;Start-Sleep -Milliseconds 350;$idleFrames=(Send $pet 0x8003 0)-$before
 if($idleFrames -gt 2){throw "Unexpected continuous idle rendering: $idleFrames frames"}
 @{upright=$upright;partial=$partial;folded=$folded;returned=$returned;idleFrames=$idleFrames}|ConvertTo-Json -Depth 3|Set-Content (Join-Path $root 'output/antenna-test.json')
 Get-Content (Join-Path $root 'output/antenna-test.json')
} finally {
 [void](Send $pet 0x202)
 & (Join-Path $PSScriptRoot 'control.ps1') -Action Exit
 foreach($entry in $saved.GetEnumerator()){[void][AntennaCheck]::WritePrivateProfileString('PICO',$entry.Key,$entry.Value,$config)}
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 [void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y);[void][EmbeddedWin]::SetThreadDpiAwarenessContext($oldDpi)
}
