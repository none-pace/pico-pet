$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'Start PICO first'}
if((Send $pet 0x8003 25) -eq 1){[void](Send $pet 0x111 244)}
[void](Send $pet 0x111 261)
[void](Send $pet 0x111 211)
$before=Send $pet 0x8003 5
[void](Send $pet 0x111 244)
if((Send $pet 0x8003 5) -ne $before){throw 'Opening terminal reset the 3D orientation'}
TypeText 'echo PICO_TV_CMD_OK';[void](Send $pet 0x102 13);Start-Sleep -Milliseconds 700
foreach($view in @(210,211)){
 [void](Send $pet 0x111 $view);Start-Sleep -Milliseconds 300
 if((Send $pet 0x8003 25) -ne 1){throw 'Changing view closed terminal'}
 $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
 $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top)
 $graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($r.Left,$r.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $PSScriptRoot "../output/terminal-projection-$view.png"))}finally{$graphics.Dispose();$bitmap.Dispose()}
}
if((Send $pet 0x8003 5) -eq 36){throw 'Terminal is still locked to front view'}
Write-Host 'PASS: terminal preserves orientation and changes 3D views while remaining open.'
[void](Send $pet 0x111 244)
