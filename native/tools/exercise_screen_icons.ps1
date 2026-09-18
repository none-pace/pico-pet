$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'PICO is not running'}
[void](Send $pet 0x111 210)
$r=New-Object EmbeddedWin+RECT
[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
$cursor=New-Object EmbeddedWin+POINT
[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
try {
 $extent=$r.Right-$r.Left
 foreach($sample in @(@(118,145,0),@(306,145,1),@(494,145,2),@(682,145,3),@(400,35,-2),@(212,145,-2),@(400,408,-2))){
  $worldX=($sample[0]/800.0-.5)*83.6
  $worldY=42.5+(.5-$sample[1]/500.0)*47.6
  [void][EmbeddedWin]::SetCursorPos(($r.Left+[int]($extent*(.5+$worldX/120))),($r.Top+[int]($extent*(.5+(44-$worldY)/120))))
  [void](Send $pet 0x200)
  if((Send $pet 0x8003 16) -ne $sample[2]){throw "Wrong icon at $($sample[0]),$($sample[1])"}
  if((Send $pet 0x8003 14) -ne 1){throw 'Desktop disappeared inside the screen'}
 }
 [void](Send $pet 0x8003 41)
 Copy-Item (Join-Path $env:TEMP 'pico-render.bmp') (Join-Path $PSScriptRoot '../output/screen-icons-fixed.bmp') -Force
 Write-Host 'PASS: four icon bounds, header, gutters and footer.'
} finally {
 [void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y)
 [void](Send $pet 0x200)
}
