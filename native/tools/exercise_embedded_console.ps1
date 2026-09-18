param([switch]$Codex)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
$oldDpi=[EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$cursor=New-Object EmbeddedWin+POINT;[void][EmbeddedWin]::GetCursorPos([ref]$cursor)
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
try {
 if($pet -eq [IntPtr]::Zero){Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden;Start-Sleep -Milliseconds 800;$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')}
 if($pet -eq [IntPtr]::Zero){throw 'PICO did not start'}
 [void](Send $pet 0x111 210);[void](Send $pet 0x111 250)
 $petRect=New-Object EmbeddedWin+RECT;[void][EmbeddedWin]::GetWindowRect($pet,[ref]$petRect);$width=$petRect.Right-$petRect.Left;$button=$null
 for($y=$petRect.Top+[int]($width*.68);$y -lt $petRect.Top+[int]($width*.84) -and !$button;$y+=2){for($x=$petRect.Left+[int]($width*.62);$x -lt $petRect.Left+[int]($width*.84);$x+=2){[void][EmbeddedWin]::SetCursorPos($x,$y);[void](Send $pet 0x200);if((Send $pet 0x8003 27) -eq 1){$button=@($x,$y);break}}}
 if(!$button){throw 'The red assistant button was not detected on the television casing'}
 [void](Send $pet 0x201);[void](Send $pet 0x202);Start-Sleep -Milliseconds 250
 if((Send $pet 0x8003 25) -ne 1){throw 'The television console did not open'}
 if([EmbeddedWin]::FindWindow('PicoPet.EmbeddedConsole','PICO 命令助手') -ne [IntPtr]::Zero){throw 'A separate console window still exists'}
 TypeText 'echo PICO_TV_CMD_OK';[void](Send $pet 0x102 13)
 for($i=0;$i -lt 100 -and ((Send $pet 0x8003 26) -ne 0 -or (Send $pet 0x8003 28) -ne 1);$i++){Start-Sleep -Milliseconds 100}
 if((Send $pet 0x8003 28) -ne 1 -or (Send $pet 0x8003 26) -ne 0){throw 'Embedded CMD output failed'}
 function AwaitIdle {for($j=0;$j -lt 150 -and (Send $pet 0x8003 26) -ne 0;$j++){Start-Sleep -Milliseconds 100};if((Send $pet 0x8003 26) -ne 0){throw 'CMD session timed out'}}
 TypeText 'set PICO_TEST_SESSION=PICO_SESSION_42';[void](Send $pet 0x102 13);AwaitIdle
 TypeText 'echo %PICO_TEST_SESSION%';[void](Send $pet 0x102 13);AwaitIdle
 if((Send $pet 0x8003 31) -ne 1){throw 'CMD environment did not persist across commands'}
 TypeText 'echo PICO_EDIT_13';[void](Send $pet 0x100 37);TypeText '2';[void](Send $pet 0x102 13);AwaitIdle
 if((Send $pet 0x8003 32) -ne 1){throw 'Insertion at the caret failed'}
 TypeText 'echo PICO_STREAM_READY& ping -n 4 127.0.0.1 >nul';[void](Send $pet 0x102 13)
 Start-Sleep -Milliseconds 1200
 if((Send $pet 0x8003 33) -ne 1 -or (Send $pet 0x8003 26) -ne 1){throw 'Output was not visible while the command was still running'}
 AwaitIdle
 if($Codex){
  [void](Send $pet 0x100 9);if((Send $pet 0x8003 30) -ne 1){throw 'TAB did not select Codex read-only mode'}
  TypeText 'Reply with exactly PICO_TV_CODEX_OK and no other text.';[void](Send $pet 0x102 13)
  for($i=0;$i -lt 1200 -and ((Send $pet 0x8003 26) -ne 0 -or (Send $pet 0x8003 29) -ne 1);$i++){Start-Sleep -Milliseconds 100}
  if((Send $pet 0x8003 29) -ne 1){throw 'Embedded Codex output failed'}
 }
 $bitmap=New-Object Drawing.Bitmap $width,$width;$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$dc=$graphics.GetHdc();$screen=[EmbeddedWin]::GetDC([IntPtr]::Zero);try{[void][EmbeddedWin]::BitBlt($dc,0,0,$width,$width,$screen,$petRect.Left,$petRect.Top,0x40CC0020)}finally{[void][EmbeddedWin]::ReleaseDC([IntPtr]::Zero,$screen);$graphics.ReleaseHdc($dc)};$bitmap.Save((Join-Path $root 'output/embedded-console.png'),[Drawing.Imaging.ImageFormat]::Png)}finally{$graphics.Dispose();$bitmap.Dispose()}
 [void](Send $pet 0x102 27);Start-Sleep -Milliseconds 200
 if((Send $pet 0x8003 25) -ne 0){throw 'The television console did not close'}
 @{embedded=$true;nativeSurface=$true;separateWindow=$false;cmd=$true;codex=[bool]$Codex;insideScreen=$true;size=$width} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/embedded-console-test.json') -Encoding utf8
 Get-Content (Join-Path $root 'output/embedded-console-test.json')
} finally {[void][EmbeddedWin]::SetCursorPos($cursor.X,$cursor.Y);[void][EmbeddedWin]::SetThreadDpiAwarenessContext($oldDpi)}
