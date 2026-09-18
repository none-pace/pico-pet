param([int]$SampleSeconds = 20, [switch]$Floating)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'output'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class PicoWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string c,string n);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr w,uint m,IntPtr p,IntPtr l);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr w,out RECT r);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr w,IntPtr after,int x,int y,int width,int height,uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr hwnd,IntPtr dc);
    [DllImport("gdi32.dll")] public static extern bool BitBlt(IntPtr destination,int x,int y,int width,int height,IntPtr source,int sx,int sy,uint operation);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
$handle = [PicoWin]::FindWindow('PicoPet.Win11.Native','PICO')
$previousDpi=[PicoWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
if ($handle -eq [IntPtr]::Zero) {
    Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden
    for ($attempt=0; $attempt -lt 20; $attempt++) {
        Start-Sleep -Milliseconds 100
        $handle = [PicoWin]::FindWindow('PicoPet.Win11.Native','PICO')
        if ($handle -ne [IntPtr]::Zero) { break }
    }
}
if ($handle -eq [IntPtr]::Zero) { throw 'PICO did not create its window.' }
function Stat([int]$index) { [PicoWin]::SendMessage($handle,0x8003,[IntPtr]$index,[IntPtr]::Zero).ToInt64() }
function Command([int]$id) { [void][PicoWin]::SendMessage($handle,0x111,[IntPtr]$id,[IntPtr]::Zero) }
Command 106
Command 220
if (((Stat 4) -band 1) -ne [int][bool]$Floating) { Command 108 }
$proc = Get-Process PicoPet | Select-Object -First 1
$initial = Get-Process -Id $proc.Id
$cpuBefore = $initial.TotalProcessorTime.TotalSeconds
$drawsBefore = Stat 0
$wakesBefore = Stat 1
$movesBefore = Stat 3
$watch = [Diagnostics.Stopwatch]::StartNew()
Start-Sleep -Seconds $SampleSeconds
$watch.Stop()
$after = Get-Process -Id $proc.Id
$cpuDelta = $after.TotalProcessorTime.TotalSeconds-$cpuBefore
$metrics = [ordered]@{
    profile = $(if($Floating){'floating'}else{'idle'})
    pid = $proc.Id
    durationSeconds = [Math]::Round($watch.Elapsed.TotalSeconds,3)
    cpuSeconds = $cpuDelta
    cpuPercentOneCore = [Math]::Round(100*$cpuDelta/$watch.Elapsed.TotalSeconds,4)
    cpuPercentMachine = [Math]::Round(100*$cpuDelta/$watch.Elapsed.TotalSeconds/[Environment]::ProcessorCount,4)
    workingSetMB = [Math]::Round($after.WorkingSet64/1MB,2)
    privateMB = [Math]::Round($after.PrivateMemorySize64/1MB,2)
    handleCount = $after.HandleCount
    redraws = (Stat 0)-$drawsBefore
    timerWakes = (Stat 1)-$wakesBefore
    windowMoves = (Stat 3)-$movesBefore
    visible = [PicoWin]::IsWindowVisible($handle)
}
Command 101
$pausedDraws = Stat 0
$pausedWakes = Stat 1
Start-Sleep -Seconds 2
$metrics.pausedRedraws = (Stat 0)-$pausedDraws
$metrics.pausedTimerWakes = (Stat 1)-$pausedWakes
Command 101
Command 100
$hiddenDraws = Stat 0
$hiddenWakes = Stat 1
Start-Sleep -Seconds 2
$metrics.hiddenRedraws = (Stat 0)-$hiddenDraws
$metrics.hiddenTimerWakes = (Stat 1)-$hiddenWakes
Command 100
Start-Sleep -Milliseconds 250
$rect = New-Object PicoWin+RECT
[void][PicoWin]::GetWindowRect($handle,[ref]$rect)
$bitmap = New-Object Drawing.Bitmap ($rect.Right-$rect.Left),($rect.Bottom-$rect.Top)
$graphics = [Drawing.Graphics]::FromImage($bitmap)
try {
    $destination=$graphics.GetHdc()
    $screen=[PicoWin]::GetDC([IntPtr]::Zero)
    try { [void][PicoWin]::BitBlt($destination,0,0,$bitmap.Width,$bitmap.Height,$screen,$rect.Left,$rect.Top,0x40CC0020) }
    finally { [void][PicoWin]::ReleaseDC([IntPtr]::Zero,$screen);$graphics.ReleaseHdc($destination) }
    $bitmap.Save((Join-Path $output 'desktop-pet.png'),[Drawing.Imaging.ImageFormat]::Png)
} finally { $graphics.Dispose();$bitmap.Dispose() }
$reportName = if($Floating){'performance-floating.json'}else{'performance.json'}
$metrics | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $output $reportName) -Encoding utf8
$metrics | ConvertTo-Json
[void][PicoWin]::SetThreadDpiAwarenessContext($previousDpi)
if ($metrics.pausedTimerWakes -ne 0 -or $metrics.hiddenTimerWakes -ne 0) { throw 'Suspended timers still woke up.' }
if ($metrics.workingSetMB -gt 50) { throw 'Working set exceeded the target.' }
