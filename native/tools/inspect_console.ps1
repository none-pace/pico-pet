$ErrorActionPreference='Stop'
Add-Type -TypeDefinition @'
using System;using System.Runtime.InteropServices;
public static class ConsoleInspect {
 [StructLayout(LayoutKind.Sequential)]public struct C{public short X,Y;}
 [StructLayout(LayoutKind.Sequential)]public struct R{public short L,T,Ri,B;}
 [StructLayout(LayoutKind.Sequential)]public struct Info{public C Size,Cursor;public ushort Attr;public R Window;public C Max;}
 [StructLayout(LayoutKind.Explicit,CharSet=CharSet.Unicode,Size=4)]public struct Cell{[FieldOffset(0)]public char Ch;[FieldOffset(2)]public ushort Attr;}
 [StructLayout(LayoutKind.Sequential)]public struct CI{public uint Size;public int Visible;}
 [DllImport("kernel32.dll")]public static extern bool FreeConsole();
 [DllImport("kernel32.dll")]public static extern bool AttachConsole(uint p);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr CreateFile(string n,uint a,uint s,IntPtr z,uint d,uint f,IntPtr t);
 [DllImport("kernel32.dll")]public static extern bool GetConsoleScreenBufferInfo(IntPtr h,out Info i);
 [DllImport("kernel32.dll")]public static extern bool GetConsoleCursorInfo(IntPtr h,out CI i);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode)]public static extern bool ReadConsoleOutput(IntPtr h,[Out]Cell[] c,C size,C pos,ref R r);
 [DllImport("kernel32.dll")]public static extern bool CloseHandle(IntPtr h);
}
'@
$pet=Get-Process PicoPet | Select-Object -First 1
$child=Get-CimInstance Win32_Process -Filter "Name='cmd.exe'" | Where-Object { $_.ParentProcessId -eq $pet.Id -and $_.CommandLine -match 'chcp 65001' } | Select-Object -First 1
if(!$child){throw 'No PICO console found'}
[void][ConsoleInspect]::FreeConsole()
if(![ConsoleInspect]::AttachConsole($child.ProcessId)){throw 'Attach failed'}
$handle=[ConsoleInspect]::CreateFile('CONOUT$',2147483648,3,[IntPtr]::Zero,3,0,[IntPtr]::Zero)
try{
 $info=New-Object ConsoleInspect+Info;$ci=New-Object ConsoleInspect+CI
 if(![ConsoleInspect]::GetConsoleScreenBufferInfo($handle,[ref]$info)){throw 'Screen buffer unavailable'}
 [void][ConsoleInspect]::GetConsoleCursorInfo($handle,[ref]$ci)
 $rect=$info.Window;$size=New-Object ConsoleInspect+C;$size.X=$rect.Ri-$rect.L+1;$size.Y=$rect.B-$rect.T+1
 $cells=New-Object 'ConsoleInspect+Cell[]' ($size.X*$size.Y)
 [void][ConsoleInspect]::ReadConsoleOutput($handle,$cells,$size,(New-Object ConsoleInspect+C),[ref]$rect)
}finally{[void][ConsoleInspect]::CloseHandle($handle);[void][ConsoleInspect]::FreeConsole()}
[pscustomobject]@{Info=$info;Cursor=$ci;Cells=$cells}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $PSScriptRoot '../output/console-state.json')
$info|ConvertTo-Json -Depth 3;$ci|ConvertTo-Json
for($y=0;$y -lt $size.Y;$y++){'{0:D2}: {1}' -f $y,(-join ($cells[($y*$size.X)..(($y+1)*$size.X-1)]|ForEach-Object {$_.Ch}))}
$cells|Group-Object Attr|Select-Object Name,Count
