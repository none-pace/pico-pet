param([switch]$Uninstall,[switch]$Quiet,[switch]$NoLaunch)
$ErrorActionPreference='Stop'
$installRoot=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'Programs\PicoPet'
$stateFile=Join-Path $installRoot 'install-state.json'
$registration='HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PicoPet.Win11'
$shortcutRoot=Join-Path ([Environment]::GetFolderPath('Programs')) 'PICO Desktop Pet'
$shortcut=Join-Path $shortcutRoot 'PICO.lnk'
$payload=@('PicoPet.exe','PicoPet.Input.dll','README.md','pet.cmd','setup.ps1')
$mutex=$null;$locked=$false;$stage=$null
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PicoSetupNative {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
 [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,SetLastError=true)] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,string l,uint f,uint timeout,out IntPtr result);
}
'@
function NotifyEnvironment {
 $result=[IntPtr]::Zero
 [void][PicoSetupNative]::SendMessageTimeout([IntPtr]0xffff,0x1a,[IntPtr]::Zero,'Environment',2,2000,[ref]$result)
}
function ReadUserPath {
 $key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Environment')
 try {
  $exists=$key -and ($key.GetValueNames() -contains 'Path')
  $kind=if($exists){$key.GetValueKind('Path')}else{[Microsoft.Win32.RegistryValueKind]::ExpandString}
  if($kind -notin @([Microsoft.Win32.RegistryValueKind]::String,[Microsoft.Win32.RegistryValueKind]::ExpandString)){throw '用户 PATH 的注册表类型不受支持。'}
  $value=if($exists){[string]$key.GetValue('Path','',[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)}else{''}
  return [pscustomobject]@{value=$value;kind=$kind;exists=[bool]$exists}
 }finally{if($key){$key.Dispose()}}
}
function WriteUserPath([string]$value,$kind,[bool]$remove=$false){
 $key=[Microsoft.Win32.Registry]::CurrentUser.CreateSubKey('Environment')
 try{if($remove){$key.DeleteValue('Path',$false)}else{$key.SetValue('Path',$value,$kind)}}finally{$key.Dispose()}
 NotifyEnvironment
}
function PathMatches([string]$entry){
 if([string]::IsNullOrWhiteSpace($entry)){return $false}
 $expanded=[Environment]::ExpandEnvironmentVariables($entry.Trim().Trim('"')).TrimEnd('\')
 return $expanded -ieq $installRoot.TrimEnd('\')
}
function StopPet {
 $window=[PicoSetupNative]::FindWindow('PicoPet.Win11.Native','PICO')
 if($window -eq [IntPtr]::Zero){return}
 [uint32]$petProcessId=0
 [void][PicoSetupNative]::GetWindowThreadProcessId($window,[ref]$petProcessId)
 $process=Get-Process -Id $petProcessId -ErrorAction SilentlyContinue
 if(!$process){return}
 [void][PicoSetupNative]::PostMessage($window,0x111,[IntPtr]107,[IntPtr]::Zero)
 if(!$process.WaitForExit(10000)){throw '桌宠尚未退出，请从托盘退出 PICO 后重试。'}
}
function Dialog([string]$message,[switch]$Confirm,[switch]$Failure){
 Add-Type -AssemblyName System.Windows.Forms
 $buttons=if($Confirm){[Windows.Forms.MessageBoxButtons]::OKCancel}else{[Windows.Forms.MessageBoxButtons]::OK}
 $icon=if($Failure){[Windows.Forms.MessageBoxIcon]::Error}else{[Windows.Forms.MessageBoxIcon]::Information}
 return [Windows.Forms.MessageBox]::Show($message,'PICO 安装程序',$buttons,$icon)
}
try {
 $sid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
 $mutex=New-Object Threading.Mutex($false,"Local\PicoPet.Setup.$sid")
 $locked=$mutex.WaitOne(0)
 if(!$locked){throw '另一个 PICO 安装或卸载任务正在运行。'}
 if((Test-Path -LiteralPath $installRoot) -and ((Get-Item -LiteralPath $installRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw '安装目录不能是目录链接。'}
 $state=$null
 if(Test-Path -LiteralPath $stateFile){
  $state=Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
  if($state.product -ne 'PicoPet.Win11' -or $state.directory -ine $installRoot){throw '安装记录与目标目录不匹配。'}
 }
 if($Uninstall){
  if(!$state){throw '没有找到当前用户的 PICO 安装记录。'}
  if(!$Quiet -and (Dialog "卸载 PICO 桌宠和 pet 命令？`r`n桌宠设置及磁盘快照数据将保留。" -Confirm) -ne [Windows.Forms.DialogResult]::OK){exit 0}
  StopPet
  $runKey='HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
  $runValue=(Get-ItemProperty -Path $runKey -ErrorAction SilentlyContinue).'PicoPet.Win11'
  $ownRun='"'+(Join-Path $installRoot 'PicoPet.exe')+'" --startup'
  if($runValue -eq $ownRun){Remove-ItemProperty -Path $runKey -Name 'PicoPet.Win11'}
  if($state.pathAdded){
   $record=ReadUserPath;$userPath=$record.value
   $remaining=@($userPath -split ';' | Where-Object {!(PathMatches $_)}) -join ';'
   $remove=($state.PSObject.Properties.Name -contains 'pathExisted') -and !$state.pathExisted -and !$remaining
   WriteUserPath $remaining $record.kind $remove
  }
  if(Test-Path -LiteralPath $shortcut){
   $shell=New-Object -ComObject WScript.Shell
   if($shell.CreateShortcut($shortcut).TargetPath -ieq (Join-Path $installRoot 'PicoPet.exe')){Remove-Item -LiteralPath $shortcut -Force}
  }
  if((Test-Path -LiteralPath $shortcutRoot) -and !(Get-ChildItem -LiteralPath $shortcutRoot -Force)){Remove-Item -LiteralPath $shortcutRoot}
  if(Test-Path -LiteralPath $registration){Remove-Item -LiteralPath $registration -Recurse}
  # Delete only known installed files; preserve user-created files and all app data.
  foreach($name in $payload+@('install-state.json')){
   $file=Join-Path $installRoot $name
   if(Test-Path -LiteralPath $file -PathType Leaf){Remove-Item -LiteralPath $file -Force}
  }
  if(!(Get-ChildItem -LiteralPath $installRoot -Force)){Remove-Item -LiteralPath $installRoot}
  if(!$Quiet){[void](Dialog "PICO 已卸载。设置和快照数据已保留。`r`n请重新打开 CMD 或 Windows Terminal，刷新命令环境。")}
  exit 0
 }
 $build=[int](Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion').CurrentBuildNumber
 $architecture=if($env:PROCESSOR_ARCHITEW6432){$env:PROCESSOR_ARCHITEW6432}else{$env:PROCESSOR_ARCHITECTURE}
 if($build -lt 22000 -or $architecture -ne 'AMD64'){throw 'PICO 仅支持 Windows 11 x64。'}
 foreach($name in $payload){if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot $name) -PathType Leaf)){throw "安装文件不完整：$name"}}
 if(!$state -and (Test-Path -LiteralPath $installRoot) -and (Get-ChildItem -LiteralPath $installRoot -Force)){throw "目标目录已存在其他文件，请先检查：$installRoot"}
 if(!$Quiet -and (Dialog "安装 PICO 桌宠到：`r`n$installRoot`r`n`r`n将添加开始菜单入口和当前用户的 pet 命令，不添加开机自启动。" -Confirm) -ne [Windows.Forms.DialogResult]::OK){exit 0}
 [void](New-Item -ItemType Directory -Path $installRoot -Force)
 $stage=Join-Path $installRoot ('.setup-'+[Guid]::NewGuid().ToString('N'))
 [void](New-Item -ItemType Directory -Path $stage)
 foreach($name in $payload){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination (Join-Path $stage $name)}
 StopPet
 foreach($name in $payload){Copy-Item -LiteralPath (Join-Path $stage $name) -Destination (Join-Path $installRoot $name) -Force}
 $record=ReadUserPath;$userPath=$record.value
 $alreadyPresent=@($userPath -split ';' | Where-Object {PathMatches $_}).Count -gt 0
 $pathAdded=if($state){[bool]$state.pathAdded}else{!$alreadyPresent}
 $pathExisted=if($state -and ($state.PSObject.Properties.Name -contains 'pathExisted')){[bool]$state.pathExisted}else{$record.exists}
 # Record PATH ownership before editing it, so reinstall/uninstall remain recoverable.
 @{product='PicoPet.Win11';directory=$installRoot;pathAdded=$pathAdded;pathExisted=$pathExisted} | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding UTF8
 if(!$alreadyPresent){
  $newPath=$installRoot+$(if($userPath){';'+$userPath}else{''})
  WriteUserPath $newPath $record.kind
 }
 [void](New-Item -ItemType Directory -Path $shortcutRoot -Force)
 $shell=New-Object -ComObject WScript.Shell
 $link=$shell.CreateShortcut($shortcut)
 $link.TargetPath=Join-Path $installRoot 'PicoPet.exe';$link.WorkingDirectory=$installRoot;$link.IconLocation=$link.TargetPath+',0';$link.Save()
 $powershell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
 $uninstallCommand='"'+$powershell+'" -NoLogo -NoProfile -WindowStyle Hidden -ExecutionPolicy Bypass -File "'+(Join-Path $installRoot 'setup.ps1')+'" -Uninstall'
 [void](New-Item -Path $registration -Force)
 $version=(Get-Item -LiteralPath (Join-Path $installRoot 'PicoPet.exe')).VersionInfo.FileVersion
 foreach($pair in @{
  DisplayName='PICO Desktop Pet';DisplayVersion=$version;Publisher='PICO';InstallLocation=$installRoot
  DisplayIcon=(Join-Path $installRoot 'PicoPet.exe');UninstallString=$uninstallCommand;QuietUninstallString=$uninstallCommand+' -Quiet'
 }.GetEnumerator()){[void](New-ItemProperty -Path $registration -Name $pair.Key -Value $pair.Value -PropertyType String -Force)}
 foreach($name in @('NoModify','NoRepair')){[void](New-ItemProperty -Path $registration -Name $name -Value 1 -PropertyType DWord -Force)}
 $size=[int][Math]::Ceiling((Get-ChildItem -LiteralPath $installRoot -File | Measure-Object Length -Sum).Sum/1KB)
 [void](New-ItemProperty -Path $registration -Name EstimatedSize -Value $size -PropertyType DWord -Force)
 if(!$NoLaunch){Start-Process -FilePath (Join-Path $installRoot 'PicoPet.exe') -WorkingDirectory $installRoot -WindowStyle Hidden}
 if(!$Quiet){[void](Dialog "安装完成。`r`n新开 CMD 后输入 pet 即可唤起桌宠。`r`n若使用 Windows Terminal，请完全退出后重新打开。`r`n也可从开始菜单打开 PICO。")}
}catch{
 if($Quiet){[Console]::Error.WriteLine($_.Exception.Message)}else{[void](Dialog $_.Exception.Message -Failure)}
 exit 1
}finally{
 if($stage -and (Test-Path -LiteralPath $stage)){
  foreach($name in $payload){$file=Join-Path $stage $name;if(Test-Path -LiteralPath $file -PathType Leaf){Remove-Item -LiteralPath $file -Force}}
  if(!(Get-ChildItem -LiteralPath $stage -Force)){Remove-Item -LiteralPath $stage}
 }
 if($locked){$mutex.ReleaseMutex()}
 if($mutex){$mutex.Dispose()}
}
