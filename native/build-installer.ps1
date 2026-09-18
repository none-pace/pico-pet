$ErrorActionPreference='Stop'
$exe=Join-Path $PSScriptRoot 'dist/PicoPet.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'Run build.ps1 first.'}
$stage=Join-Path $PSScriptRoot 'build/setup'
[void](New-Item -ItemType Directory -Path $stage -Force)
Copy-Item -LiteralPath $exe -Destination (Join-Path $stage 'PicoPet.exe') -Force
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $stage 'README.md') -Force
foreach($file in @('setup.ps1','pet.cmd')){
 Copy-Item -LiteralPath (Join-Path $PSScriptRoot "installer/$file") -Destination (Join-Path $stage $file) -Force
}
$target=Join-Path $PSScriptRoot 'dist/PICO-Setup.exe'
$sed=Join-Path $stage 'PICO-Setup.sed'
$config=@"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=1
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=
TargetName=$target
FriendlyName=PICO Desktop Pet Setup
AppLaunched=powershell.exe -NoLogo -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File setup.ps1
PostInstallCmd=<None>
AdminQuietInstCmd=powershell.exe -NoLogo -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File setup.ps1 -Quiet -NoLaunch
UserQuietInstCmd=powershell.exe -NoLogo -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File setup.ps1 -Quiet -NoLaunch
FILE0="PicoPet.exe"
FILE1="README.md"
FILE2="pet.cmd"
FILE3="setup.ps1"
[SourceFiles]
SourceFiles0=$stage\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
%FILE3%=
"@
[IO.File]::WriteAllText($sed,($config -replace "`r?`n","`r`n")+"`r`n",[Text.Encoding]::Default)
$iexpress=Join-Path $env:SystemRoot 'System32/iexpress.exe'
$build=Start-Process -FilePath $iexpress -ArgumentList '/N /Q PICO-Setup.sed' -WorkingDirectory $stage -WindowStyle Hidden -PassThru -Wait
if($build.ExitCode -ne 0 -or !(Test-Path -LiteralPath $target)){throw "IExpress installer build failed: $($build.ExitCode)"}
Write-Host "Installer: $target"
