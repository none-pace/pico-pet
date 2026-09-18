$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Embedded
$root=Split-Path $PSScriptRoot -Parent
$config=Join-Path $env:LOCALAPPDATA 'PicoPet/settings.ini'
function FindPet {
 for($i=0;$i -lt 50;$i++){$script:pet=[EmbeddedWin]::FindWindow('PicoPet.Win11.Native','PICO');if($pet -ne [IntPtr]::Zero){return};Start-Sleep -Milliseconds 100}
 throw 'PICO did not start'
}
function OpenPreferences {
 [void](Send $pet 0x111 300)
 $script:dialog=[EmbeddedWin]::FindClass('PicoPet.Preferences')
 if($dialog -eq [IntPtr]::Zero){throw 'Preferences did not open'}
}
function SetNumber([int]$id,[string]$value){
 $c=[EmbeddedWin]::GetDlgItem($dialog,$id)
 [void][EmbeddedWin]::SendText($c,0xC,[IntPtr]::Zero,$value)
 Start-Sleep -Milliseconds 650
}
function SelectChoice([int]$id,[int]$index){
 $c=[EmbeddedWin]::GetDlgItem($dialog,$id)
 [void](Send $c 0x14E $index)
 [void][EmbeddedWin]::SendMessage($dialog,0x111,[IntPtr]($id -bor (1 -shl 16)),$c)
}
function ReadNumber([int]$id){$s=New-Object Text.StringBuilder 64;[void][EmbeddedWin]::ReadText([EmbeddedWin]::GetDlgItem($dialog,$id),0xD,[IntPtr]64,$s);$s.ToString()}
FindPet
OpenPreferences
$original=@{}
foreach($id in @(1000,1007,1008,1009,1013,1014,1015)){$original[$id]=ReadNumber $id}
$pause=Send ([EmbeddedWin]::GetDlgItem($dialog,1011)) 0x147
$material=Send ([EmbeddedWin]::GetDlgItem($dialog,1002)) 0x147
try {
 SetNumber 1009 '45';SetNumber 1007 '17';SetNumber 1008 '6';SetNumber 1013 '125';SetNumber 1014 '75';SetNumber 1015 '80'
 SelectChoice 1002 1;SelectChoice 1011 1
 $text=Get-Content $config -Raw
 foreach($entry in @('frameRate=45','yaw=17000','pitch=6000','rotationSensitivity=125','motionAmplitude=75','throwGain=80','paused=1','material=1')){if($text -notmatch [regex]::Escape($entry)){throw "Autosave missing: $entry"}}
 SetNumber 1009 '999'
 if((Get-Content $config -Raw) -notmatch 'frameRate=45'){throw 'Invalid frame rate was persisted'}
 [void](Send $dialog 0x10)
 if([EmbeddedWin]::FindClass('PicoPet.Preferences') -ne [IntPtr]::Zero){throw 'Invalid input prevented preferences from closing'}
 [void](Send $pet 0x111 107)
 Get-Process PicoPet -ErrorAction SilentlyContinue|Wait-Process -Timeout 10
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 FindPet
 OpenPreferences
 if((ReadNumber 1009) -ne '45' -or (ReadNumber 1007) -ne '17' -or (ReadNumber 1008) -ne '6'){throw 'Numeric settings did not survive restart'}
 if((Send $pet 0x8003 46) -ne 1 -or ((Send $pet 0x8003 2) -band 4) -eq 0){throw 'Material or pause state did not survive restart'}
 if((Get-Content $config -Raw) -notmatch '\[System\]'){throw 'System settings were removed'}
 Write-Host 'PASS: automatic numeric and choice saving, range rejection, restart persistence, system section preservation.'
} finally {
 FindPet
 OpenPreferences
 foreach($id in $original.Keys){SetNumber $id $original[$id]}
 SelectChoice 1002 $material;SelectChoice 1011 $pause
 [void](Send $dialog 0x10)
}
