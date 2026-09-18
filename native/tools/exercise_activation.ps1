$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if((Send $pet 0x8003 25) -eq 1){[void](Send $pet 0x111 244)}
[void](Send $pet 0x111 210);[void](Send $pet 0x111 250)
$r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
$cursor=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
try{
 $width=$r.Right-$r.Left
 [void][EmbeddedWin]::SetCursorPos(($r.Left+[int]($width*(.5+35.0/120))),($r.Top+[int]($width*(.5+(44-13.5)/120))))
 [void](Send $pet 0x200)
 if((Send $pet 0x8003 38) -ne 1){throw 'Button hover state was not rendered'}
 [void](Send $pet 0x201)
 if((Send $pet 0x8003 38) -ne 2){throw 'Button pressed state was not rendered'}
 [void](Send $pet 0x202)
 if((Send $pet 0x8003 38) -ne 3 -or (Send $pet 0x8003 25) -ne 1){throw 'Button active state was not rendered'}
 [void](Send $pet 0x8003 41)
 Copy-Item (Join-Path $env:TEMP 'pico-render.bmp') (Join-Path $PSScriptRoot '../output/button-active.bmp') -Force
 [void](Send $pet 0x111 244)
 Write-Host 'PASS: physical button hover, depression and active feedback.'
}finally{[void](Send $pet 0x202);[void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y)}
