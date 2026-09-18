param([ValidateSet('Performance','Diagnostics')][string]$Page='Performance')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class FinishDesk {
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
}
'@
$pet=[FinishDesk]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden;Start-Sleep -Milliseconds 500;$pet=[FinishDesk]::FindWindow('PicoPet.Win11.Native','PICO')}
[void][FinishDesk]::SendMessage($pet,0x111,[IntPtr]250,[IntPtr]::Zero)
[void][FinishDesk]::SendMessage($pet,0x111,[IntPtr]$(if($Page -eq 'Diagnostics'){243}else{240}),[IntPtr]::Zero)
Get-Process PicoPet | Select-Object Id,WorkingSet64,Path
