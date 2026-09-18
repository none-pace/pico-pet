$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MotionSettingsWin {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
function FindPet {
 for($i=0;$i -lt 60;$i++){Start-Sleep -Milliseconds 100;$script:pet=[MotionSettingsWin]::FindWindow('PicoPet.Win11.Native','PICO');if($pet -ne [IntPtr]::Zero){return}}
 throw 'PICO did not start'
}
function Send([int]$message,[int]$argument=0){[MotionSettingsWin]::SendMessage($pet,[uint32]$message,[IntPtr]$argument,[IntPtr]::Zero).ToInt64()}
FindPet
$originalFps=Send 0x8003 17;$originalThreshold=Send 0x8003 18
try {
 foreach($pair in @(@(270,15),@(271,30),@(272,60))){[void](Send 0x111 $pair[0]);if((Send 0x8003 17) -ne $pair[1]){throw "Frame rate command $($pair[1]) failed"}}
 foreach($pair in @(@(280,1),@(281,2),@(282,4))){[void](Send 0x111 $pair[0]);if((Send 0x8003 18) -ne $pair[1]){throw "Pixel threshold command $($pair[1]) failed"}}
 [void](Send 0x111 270);[void](Send 0x111 282);[void](Send 0x111 107)
 Get-Process PicoPet -ErrorAction SilentlyContinue | Wait-Process -Timeout 10
 Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
 FindPet
 if((Send 0x8003 17) -ne 15 -or (Send 0x8003 18) -ne 4){throw 'Motion settings did not persist after restart'}
 @{frameRates=@(15,30,60);pixelThresholds=@(1,2,4);restartPersistence=$true} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root 'output/motion-settings-test.json') -Encoding utf8
 Get-Content -LiteralPath (Join-Path $root 'output/motion-settings-test.json')
}finally{
 if($pet -ne [IntPtr]::Zero){$fpsCommand=@{15=270;30=271;60=272}[[int]$originalFps];$thresholdCommand=@{1=280;2=281;4=282}[[int]$originalThreshold];[void](Send 0x111 $fpsCommand);[void](Send 0x111 $thresholdCommand)}
}
