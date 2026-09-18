$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
Add-Type 'using System;using System.Runtime.InteropServices;public static class CursorFocus{[DllImport("user32.dll")]public static extern bool SetForegroundWindow(IntPtr h);}'
$script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'PICO is not running'}
if((Send $pet 0x8003 25) -eq 1){[void](Send $pet 0x111 244)}
[void](Send $pet 0x111 210);[void](Send $pet 0x111 244)
[void][CursorFocus]::SetForegroundWindow($pet);Start-Sleep -Milliseconds 300
try{
 if((Send $pet 0x8003 44) -ne 1){throw 'CMD caret is missing while focused'}
 $before=Send $pet 0x8003 45
 TypeText 'abc';Start-Sleep -Milliseconds 150
 if((Send $pet 0x8003 45) -eq $before){throw 'CMD caret did not follow text'}
 for($i=0;$i -lt 3;$i++){[void](Send $pet 0x102 8)}
 TypeText 'opencode';[void](Send $pet 0x102 13)
 Start-Sleep -Seconds 7
 if((Send $pet 0x8003 34) -ne 1 -or (Send $pet 0x8003 44) -ne 1){throw 'OpenCode caret is missing'}
 $before=Send $pet 0x8003 45
 TypeText 'abc';Start-Sleep -Milliseconds 350
 if((Send $pet 0x8003 45) -eq $before){throw 'OpenCode caret did not follow text'}
 $afterText=Send $pet 0x8003 45
 [void](Send $pet 0x8003 41)
 Copy-Item (Join-Path $env:TEMP 'pico-render.bmp') (Join-Path $PSScriptRoot '../output/cursor-opencode.bmp') -Force
 [void](Send $pet 0x100 37);Start-Sleep -Milliseconds 350
 $afterLeft=Send $pet 0x8003 45
 if($afterLeft -ge $afterText -or $afterLeft -le $before){throw 'Arrow editing did not move the caret left by one cell'}
 [void](Send $pet 0x8)
 if((Send $pet 0x8003 44) -ne 0){throw 'Caret remained after focus loss'}
 [void](Send $pet 0x7)
 if((Send $pet 0x8003 44) -ne 1){throw 'Caret did not return on focus gain'}
 Write-Host 'PASS: CMD and OpenCode caret, text position, focus loss and regain.'
}finally{[void](Send $pet 0x111 244)}
