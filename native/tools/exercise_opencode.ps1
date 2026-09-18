$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'Start the built pet before this test'}
if((Send $pet 0x8003 25) -eq 1){[void](Send $pet 0x111 244)}
[void](Send $pet 0x111 244)
try {
 TypeText 'opencode';[void](Send $pet 0x102 13)
 for($i=0;$i -lt 100 -and (Send $pet 0x8003 34) -ne 1;$i++){Start-Sleep -Milliseconds 100}
 if((Send $pet 0x8003 34) -ne 1){throw 'Interactive console failed to attach'}
 Start-Sleep -Seconds 6
 TypeText 'PICO_TV_CMD_OK'
 for($i=0;$i -lt 50 -and (Send $pet 0x8003 28) -ne 1;$i++){Start-Sleep -Milliseconds 100}
 if((Send $pet 0x8003 28) -ne 1){throw 'Typed text was not found in the actual console screen buffer'}
 [void](Send $pet 0x102 8);Start-Sleep -Milliseconds 300
 if((Send $pet 0x8003 28) -ne 0){throw 'Backspace did not update the console screen buffer'}
 TypeText 'K';Start-Sleep -Milliseconds 300
 for($i=0;$i -lt 14;$i++){[void](Send $pet 0x102 8)}
 $sample='你好，电视助手。中文输入测试'
 foreach($character in $sample.ToCharArray()){[void](Send $pet 0x286 ([int]$character))}
 for($i=0;$i -lt 50 -and (Send $pet 0x8003 43) -ne 1;$i++){Start-Sleep -Milliseconds 100}
 if((Send $pet 0x8003 43) -ne 1){throw 'Unicode IME input did not match console screen text'}
 [void](Send $pet 0x102 8);Start-Sleep -Milliseconds 300
 if((Send $pet 0x8003 43) -ne 0){throw 'Chinese backspace did not remove the final character'}
 [void](Send $pet 0x286 ([int]$sample[$sample.Length-1]));Start-Sleep -Milliseconds 300
 [void](Send $pet 0x8003 41)
 Copy-Item (Join-Path $env:TEMP 'pico-render.bmp') (Join-Path $PSScriptRoot '../output/opencode-unicode.bmp') -Force
 $r=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$r)
 $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top)
 $graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$graphics.CopyFromScreen($r.Left,$r.Top,0,0,$bitmap.Size);$bitmap.Save((Join-Path $PSScriptRoot '../output/opencode-input.png'))}finally{$graphics.Dispose();$bitmap.Dispose()}
 Write-Host 'PASS: OpenCode screen buffer, English and Chinese IME input, backspace; no prompt submitted.'
}finally{[void](Send $pet 0x111 244)}
if((Send $pet 0x8003 34) -ne 0){throw 'Interactive console did not close'}
