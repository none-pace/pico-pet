$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
[void](New-Item -ItemType Directory -Path (Join-Path $root 'output') -Force)
foreach($mode in @('self-test','system-test')){
 $report=Join-Path $root "output/$mode.json"
 $process=Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -ArgumentList "--$mode",$report -WindowStyle Hidden -Wait -PassThru
 Get-Content -LiteralPath $report
 if($process.ExitCode -ne 0){throw "$mode failed: $($process.ExitCode)"}
}
