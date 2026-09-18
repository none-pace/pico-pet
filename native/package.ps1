$ErrorActionPreference = 'Stop'
$exe = Join-Path $PSScriptRoot 'dist/PicoPet.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Run build.ps1 first.' }
$readme = Join-Path $PSScriptRoot 'dist/README.md'
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination $readme -Force
$archive = Join-Path $PSScriptRoot 'dist/PICO-Win11-x64.zip'
Compress-Archive -LiteralPath $exe,$readme -DestinationPath $archive -Force
Write-Host "Portable package: $archive"
& (Join-Path $PSScriptRoot 'build-installer.ps1')
