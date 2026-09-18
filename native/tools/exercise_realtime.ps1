$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if((Send $pet 0x8003 35) -ne 1){throw 'Realtime renderer is not active'}
[void](Send $pet 0x111 261);[void](Send $pet 0x111 250);[void](Send $pet 0x111 210)
if((Send $pet 0x8003 40) -lt 10000){throw 'Model rendered blank'}
$r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
$cursor=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
$x=$r.Left+[int](($r.Right-$r.Left)*.5);$y=$r.Top+[int](($r.Bottom-$r.Top)*.35)
$frames=Send $pet 0x8003 0;$time=Send $pet 0x8003 36
$clock=[Diagnostics.Stopwatch]::StartNew()
try{
 [void][EmbeddedWin]::SetCursorPos($x,$y);[void](Send $pet 0x207)
 for($i=0;$i -lt 180;$i++){
  [void][EmbeddedWin]::SetCursorPos(($x+[int](90*[Math]::Sin($i/35.0))),($y+[int](15*[Math]::Cos($i/40.0))))
  [void](Send $pet 0x200);Start-Sleep -Milliseconds 16
 }
}finally{[void](Send $pet 0x208);[void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y)}
$seconds=$clock.Elapsed.TotalSeconds;$count=(Send $pet 0x8003 0)-$frames
$report=@{frames=$count;seconds=$seconds;fps=$count/$seconds;meanRenderMs=((Send $pet 0x8003 36)-$time)/1000/[Math]::Max(1,$count);maxRenderMs=(Send $pet 0x8003 37)/1000;visiblePixels=(Send $pet 0x8003 40)}
$report|ConvertTo-Json|Set-Content (Join-Path $PSScriptRoot '../output/realtime-performance.json')
$report|ConvertTo-Json
if($count -lt 80){throw 'Continuous rotation produced too few frames'}
[void](Send $pet 0x111 210);[void](Send $pet 0x8003 41)
Copy-Item (Join-Path $env:TEMP 'pico-render.bmp') (Join-Path $PSScriptRoot '../output/realtime-model.bmp') -Force
