param([switch]$AllowExistingInstall)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$installRoot=Join-Path $env:LOCALAPPDATA 'Programs/PicoPet'
$setup=Join-Path $root 'dist/PICO-Setup.exe'
$state=Join-Path $installRoot 'install-state.json'
$windowsPowerShell=Join-Path $env:SystemRoot 'System32/WindowsPowerShell/v1.0/powershell.exe'
$beforePath=[Environment]::GetEnvironmentVariable('Path','User')
$pathKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
$rawBefore=$pathKey.GetValue('Path','',[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
$kindBefore=$pathKey.GetValueKind('Path');$pathKey.Dispose()
$hadInstallation=Test-Path -LiteralPath $state
if($hadInstallation -and !$AllowExistingInstall){throw 'This lifecycle test needs a new install, or explicit -AllowExistingInstall.'}
$legacy=Join-Path $env:LOCALAPPDATA 'Microsoft/WinGet/Links/pet.cmd'
$legacyHash=if(Test-Path -LiteralPath $legacy){(Get-FileHash -LiteralPath $legacy).Hash}else{''}
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class InstallCheck {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
function Install {
 $process=Start-Process -FilePath $setup -ArgumentList '/Q' -WindowStyle Hidden -PassThru
 if(!$process.WaitForExit(60000)){throw 'Installer did not finish'}
 if($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath $state)){throw "Installer failed: $($process.ExitCode)"}
 $exe=Join-Path $installRoot 'PicoPet.exe'
 if((Get-FileHash -LiteralPath $exe).Hash -ne (Get-FileHash -LiteralPath (Join-Path $root 'dist/PicoPet.exe')).Hash){throw 'Installed EXE does not match the build'}
}
function StartPetCommand {
 $env:PATH=[Environment]::GetEnvironmentVariable('Path','Machine')+';'+[Environment]::GetEnvironmentVariable('Path','User')
 $located=@(& $env:ComSpec /d /c 'where pet')
 if($located[0] -ine (Join-Path $installRoot 'pet.cmd')){throw "CMD resolved another pet command: $($located[0])"}
 $command=Start-Process -FilePath $env:ComSpec -ArgumentList '/d /c pet' -WorkingDirectory $env:TEMP -WindowStyle Hidden -PassThru
 if(!$command.WaitForExit(5000) -or $command.ExitCode -ne 0){throw 'pet did not return promptly to CMD'}
 for($i=0;$i -lt 50;$i++){
  Start-Sleep -Milliseconds 100
  $window=[InstallCheck]::FindWindow('PicoPet.Win11.Native','PICO')
  if($window -ne [IntPtr]::Zero){return $window}
 }
 throw 'pet did not launch the desktop pet'
}
Install
$registration=Get-ItemProperty 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/PicoPet.Win11'
if($registration.DisplayName -ne 'PICO Desktop Pet' -or !$registration.QuietUninstallString){throw 'Windows uninstall registration is missing'}
$link=Join-Path ([Environment]::GetFolderPath('Programs')) 'PICO Desktop Pet/PICO.lnk'
if(!(Test-Path -LiteralPath $link)){throw 'Start menu shortcut was not installed'}
$window=StartPetCommand
[uint32]$firstId=0
[void][InstallCheck]::GetWindowThreadProcessId($window,[ref]$firstId)
if((Get-Process -Id $firstId).Path -ine (Join-Path $installRoot 'PicoPet.exe')){throw 'pet launched an older copy'}
[void][InstallCheck]::PostMessage($window,0x111,[IntPtr]100,[IntPtr]::Zero)
Start-Sleep -Milliseconds 150
if([InstallCheck]::IsWindowVisible($window)){throw 'Hide did not take effect before wake test'}
$window=StartPetCommand
Start-Sleep -Milliseconds 200
[uint32]$secondId=0
[void][InstallCheck]::GetWindowThreadProcessId($window,[ref]$secondId)
if($firstId -ne $secondId -or ![InstallCheck]::IsWindowVisible($window)){throw 'pet did not wake the existing instance'}
Install
$installedPath=[Environment]::GetEnvironmentVariable('Path','User')
if(@($installedPath -split ';' | Where-Object {$_.TrimEnd('\') -ieq $installRoot.TrimEnd('\')}).Count -ne 1){throw 'Reinstall duplicated the PATH entry'}
$data=Join-Path $env:LOCALAPPDATA 'PicoPet'
$hadSettings=Test-Path -LiteralPath (Join-Path $data 'settings.ini')
$hadIndex=Test-Path -LiteralPath (Join-Path $data 'index.db')
$uninstaller=Start-Process -FilePath $windowsPowerShell -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+(Join-Path $installRoot 'setup.ps1')+'"'),'-Uninstall','-Quiet') -WindowStyle Hidden -PassThru
if(!$uninstaller.WaitForExit(20000) -or $uninstaller.ExitCode -ne 0){throw 'Uninstall failed'}
if((Test-Path -LiteralPath $state) -or (Test-Path -LiteralPath $link) -or (Test-Path 'HKCU:/Software/Microsoft/Windows/CurrentVersion/Uninstall/PicoPet.Win11')){throw 'Uninstall left owned registration or files'}
if(!$hadInstallation -and [Environment]::GetEnvironmentVariable('Path','User') -cne $beforePath){throw 'Uninstall changed unrelated PATH entries'}
$pathKey=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
$rawAfter=$pathKey.GetValue('Path','',[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
$kindAfter=$pathKey.GetValueKind('Path');$pathKey.Dispose()
$expectedRaw=if($hadInstallation){@($rawBefore -split ';' | Where-Object {$_.TrimEnd('\') -ine $installRoot.TrimEnd('\')}) -join ';'}else{$rawBefore}
if($rawAfter -cne $expectedRaw -or $kindAfter -ne $kindBefore){throw 'Uninstall changed PATH spelling or registry value kind'}
if($hadSettings -and !(Test-Path -LiteralPath (Join-Path $data 'settings.ini'))){throw 'Uninstall removed settings'}
if($hadIndex -and !(Test-Path -LiteralPath (Join-Path $data 'index.db'))){throw 'Uninstall removed snapshot data'}
if($legacyHash -and (Get-FileHash -LiteralPath $legacy).Hash -ne $legacyHash){throw 'Legacy pet command was overwritten'}
Install
$window=StartPetCommand
$report=@{installer=$true;cmdLaunch=$true;singleInstanceWake=$true;reinstall=$true;uninstall=$true;pathRestored=$true;legacyPreserved=$true;settingsRetained=$hadSettings;snapshotsRetained=$hadIndex;installDirectory=$installRoot}
$report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/install-test.json') -Encoding utf8
$report | ConvertTo-Json
