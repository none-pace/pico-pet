$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
$r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
$bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top)
$g=[Drawing.Graphics]::FromImage($bitmap)
try{$dc=$g.GetHdc();[void][EmbeddedWin]::SendMessage($pet,0x318,$dc,[IntPtr]4);$g.ReleaseHdc($dc);$bitmap.Save((Join-Path $PSScriptRoot '../output/realtime-direct.png'))}finally{$g.Dispose();$bitmap.Dispose()}
@{visiblePixels=(Send $pet 0x8003 40);realtime=(Send $pet 0x8003 35);draws=(Send $pet 0x8003 0);maxMs=(Send $pet 0x8003 37)/1000} | ConvertTo-Json
