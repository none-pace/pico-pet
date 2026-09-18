$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'test_support.ps1') -Mode Monitor
$root=Split-Path $PSScriptRoot -Parent
Add-Type @'
using System;using System.Net;using System.Net.Sockets;using System.Runtime.InteropServices;
public static class CaptureFixture { public static UdpClient Receiver,Sender; public static int Port;
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern IntPtr FindWindow(string c,string t);
 public static IntPtr Desk(){return FindWindow("PicoPet.SystemDesk",null);}
 public static void Start(){Receiver=new UdpClient(new IPEndPoint(IPAddress.Loopback,0));Port=((IPEndPoint)Receiver.Client.LocalEndPoint).Port;Sender=new UdpClient();Sender.Send(new byte[1377],1377,new IPEndPoint(IPAddress.Loopback,Port));Receiver.Client.ReceiveTimeout=1000;IPEndPoint from=null;Receiver.Receive(ref from);}
 public static void Stop(){if(Sender!=null)Sender.Close();if(Receiver!=null)Receiver.Close();} }
'@
function Await([scriptblock]$condition,[string]$failure){for($i=0;$i -lt 100;$i++){if(& $condition){return};Start-Sleep -Milliseconds 100};throw $failure}
function FirstRow {[MonitorWin]::SendMessage((Control 3019),0x1004,[IntPtr]::Zero,[IntPtr]::Zero).ToInt32()}
$pet=[MonitorWin]::FindWindow('PicoPet.Win11.Native','PICO');$desk=[IntPtr]::Zero
try {
 [void](Message $pet 0x111 242);Await { $script:desk=[CaptureFixture]::Desk();$desk -ne [IntPtr]::Zero } 'System panel missing'
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr]::Zero,20,20,1560,980,0x14)
 Choose 3070 0;Choose 3017 6;Start-Sleep -Milliseconds 1200
 [CaptureFixture]::Start();$port=[CaptureFixture]::Port;[CaptureFixture]::Stop();SetFilter 3021 ([string]$port)
 Await { (FirstRow) -gt 0 } 'Real-time capture did not receive UDP event'
 $rows=FirstRow;$captured=$false
 if($rows -gt 0){[void](Message ([MonitorWin]::GetDlgItem((Control 3071),32000)) 0xF5);[void](Message (Control 3069) 0xF1 1);[void](Message $desk 0x111 3069);Start-Sleep -Milliseconds 120;$captured=(ReadControl 3032) -match '1377'}
 if(!$captured){throw ('Capture view opened without event evidence: '+(ReadControl 3032))}
 Capture 'network-event-capture'
 $process=Get-Process PicoPet | Select-Object -First 1;$process.Refresh();$cpu=$process.TotalProcessorTime.TotalMilliseconds;Start-Sleep -Milliseconds 3100;$process.Refresh();$cpuMs=$process.TotalProcessorTime.TotalMilliseconds-$cpu
 [void][MonitorWin]::ShowWindow($desk,6);Start-Sleep -Milliseconds 1500
 $activeSessions=(& logman query -ets | Out-String)
 if($activeSessions.Contains('PicoPet.Network.'+$process.Id+'.')){throw 'Capture continued while minimized'}
 @{captureView=$true;udpPort=$port;rows=$rows;eventEvidence=$captured;captureCpuMsPer3Seconds=$cpuMs;stopsWhenMinimized=$true}|ConvertTo-Json|Set-Content (Join-Path $root 'output/network-capture-checks.json')
 Get-Content (Join-Path $root 'output/network-capture-checks.json')
} finally {[CaptureFixture]::Stop();if($desk -ne [IntPtr]::Zero){[void](Message $desk 0x10)};[void][MonitorWin]::SetThreadDpiAwarenessContext($oldDpi)}
