$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
[void][EmbeddedWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){throw 'PICO is not running'}
$original=Send $pet 0x8003 46
$yaw=Send $pet 0x8003 39
$names=@('plastic','metal','glass','ceramic')
$hashes=@()
try {
 for($i=0;$i -lt 4;$i++) {
  [void](Send $pet 0x111 (290+$i))
  if((Send $pet 0x8003 46) -ne $i){throw 'Material selection failed'}
  if((Send $pet 0x8003 39) -ne $yaw){throw 'Material changed model orientation'}
  [void](Send $pet 0x8003 41)
  $path=Join-Path $PSScriptRoot "../output/material-$($names[$i]).bmp"
  Copy-Item (Join-Path $env:TEMP 'pico-render.bmp') $path -Force
  $hashes+=(Get-FileHash -LiteralPath $path).Hash
 }
 if(($hashes|Select-Object -Unique).Count -ne 4){throw 'Material variants are identical'}
 Write-Host 'PASS: four distinct materials, selection and orientation preservation.'
} finally {[void](Send $pet 0x111 (290+$original))}
