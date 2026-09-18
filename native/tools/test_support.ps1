# Shared Win32 test harness. Dot-source; run UI tests in separate PowerShell processes.
param([ValidateSet('Embedded','Monitor')][string]$Mode='Embedded')
$root=Split-Path $PSScriptRoot -Parent
[void](New-Item -ItemType Directory -Path (Join-Path $root 'output') -Force)
if($Mode -eq 'Embedded') {
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class EmbeddedWin {
 [StructLayout(LayoutKind.Sequential)]public struct POINT{public int X,Y;}
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 public static IntPtr FindClass(string c){return FindWindow(c,null);}
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,System.Text.StringBuilder l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")]public static extern bool GetCursorPos(out POINT p);
 [DllImport("user32.dll")]public static extern bool SetCursorPos(int x,int y);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr d);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr d,int x,int y,int w,int h,IntPtr s,int sx,int sy,uint op);
}
'@
function Send([IntPtr]$h,[uint32]$message,[int64]$argument=0){[EmbeddedWin]::SendMessage($h,$message,[IntPtr]$argument,[IntPtr]::Zero).ToInt64()}
function TypeText([string]$value){foreach($char in $value.ToCharArray()){[void](Send $script:pet 0x102 ([int]$char))}}

} else {
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Runtime.InteropServices;
public static class MonitorWin {
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [StructLayout(LayoutKind.Sequential)]public struct RECT{public int Left,Top,Right,Bottom;}
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern IntPtr FindWindow(string c,string t);
 [DllImport("user32.dll")]public static extern IntPtr GetDlgItem(IntPtr h,int id);
 [DllImport("user32.dll")]public static extern bool EnumChildWindows(IntPtr h,EnumProc p,IntPtr l);
 [DllImport("user32.dll")]public static extern int GetDlgCtrlID(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]public static extern int GetClassName(IntPtr h,System.Text.StringBuilder s,int n);
 [DllImport("user32.dll")]public static extern IntPtr SendMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")]public static extern bool IsWindow(IntPtr h);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr SendText(IntPtr h,uint m,IntPtr w,string l);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]public static extern IntPtr ReadText(IntPtr h,uint m,IntPtr w,System.Text.StringBuilder l);
 [DllImport("user32.dll")]public static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]public static extern bool GetWindowRect(IntPtr h,out RECT r);
 [DllImport("user32.dll")]public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int w,int s,uint flags);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr h,int c);
 [DllImport("user32.dll")]public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr c);
 [DllImport("user32.dll")]public static extern IntPtr GetDC(IntPtr h);
 [DllImport("user32.dll")]public static extern int ReleaseDC(IntPtr h,IntPtr d);
 [DllImport("gdi32.dll")]public static extern bool BitBlt(IntPtr d,int x,int y,int w,int h,IntPtr s,int sx,int sy,uint op);
 public static IntPtr FindFilename(IntPtr dialog){
  IntPtr result=IntPtr.Zero;
  EnumChildWindows(dialog,(h,p)=>{var cls=new System.Text.StringBuilder(256);GetClassName(h,cls,256);int id=GetDlgCtrlID(h);if(cls.ToString()=="Edit" && (id==1001 || id==1148)){result=h;return false;}return true;},IntPtr.Zero);
  return result;
 }
 static Thread transfer;static volatile bool running;
 static TcpListener listener;static TcpClient client,server;
 public static int Port;
 public static void StartTraffic(){
  listener=new TcpListener(IPAddress.Loopback,0);listener.Start();Port=((IPEndPoint)listener.LocalEndpoint).Port;
  client=new TcpClient();client.Connect(IPAddress.Loopback,Port);server=listener.AcceptTcpClient();running=true;
  transfer=new Thread(()=>{var bytes=new byte[4096];try{while(running){client.GetStream().Write(bytes,0,bytes.Length);int received=0;while(received<bytes.Length){int n=server.GetStream().Read(bytes,received,bytes.Length-received);if(n<=0)return;received+=n;}Thread.Sleep(50);}}catch(System.IO.IOException){}catch(ObjectDisposedException){}});
  transfer.IsBackground=true;transfer.Start();
 }
 public static void StopTraffic(){running=false;if(client!=null)client.Close();if(server!=null)server.Close();if(listener!=null)listener.Stop();if(transfer!=null)transfer.Join(2000);}
}
'@
$oldDpi=[MonitorWin]::SetThreadDpiAwarenessContext([IntPtr](-4))
$pet=[MonitorWin]::FindWindow('PicoPet.Win11.Native','PICO')
if($pet -eq [IntPtr]::Zero){Start-Process -FilePath (Join-Path $root 'dist/PicoPet.exe') -WindowStyle Hidden;Start-Sleep -Milliseconds 700;$pet=[MonitorWin]::FindWindow('PicoPet.Win11.Native','PICO')}
function Message([IntPtr]$h,[uint32]$m,[int]$v=0){[MonitorWin]::SendMessage($h,$m,[IntPtr]$v,[IntPtr]::Zero).ToInt64()}
function Control([int]$id){[MonitorWin]::GetDlgItem($script:desk,$id)}
function Choose([int]$id,[int]$item){[void](Message (Control $id) 0x14E $item);[void](Message $script:desk 0x111 ($id -bor (1 -shl 16)))}
function SetFilter([int]$id,[string]$value){[void][MonitorWin]::SendText((Control $id),0xC,[IntPtr]::Zero,$value)}
function Rows {Message (Control 3019) 0x1004}
function Columns {$header=[MonitorWin]::SendMessage((Control 3019),0x101F,[IntPtr]::Zero,[IntPtr]::Zero);Message $header 0x1200}
function ClickFirstRow {
 $list=Control 3019
 [void][MonitorWin]::PostMessage($list,0x201,[IntPtr]1,[IntPtr]((60 -shl 16) -bor 50))
 [void][MonitorWin]::PostMessage($list,0x202,[IntPtr]::Zero,[IntPtr]((60 -shl 16) -bor 50))
 Start-Sleep -Milliseconds 150
}
function ReadControl([int]$id){$text=New-Object Text.StringBuilder 65536;[void][MonitorWin]::ReadText((Control $id),0xD,[IntPtr]65536,$text);$text.ToString()}
function DiskTab([int]$value){
 $tab=Control 3038;$scale=[MonitorWin]::GetDpiForWindow($tab)/96
 $x=[int]((30+$value*42)*$scale);$y=[int](12*$scale);$point=($y -shl 16) -bor $x
 [void][MonitorWin]::PostMessage($tab,0x201,[IntPtr]1,[IntPtr]$point)
 [void][MonitorWin]::PostMessage($tab,0x202,[IntPtr]::Zero,[IntPtr]$point)
 Start-Sleep -Milliseconds 150
 if((Message $tab 0x130B) -ne $value){throw 'Disk view tab did not change'}
}
function Capture([string]$name){
 if(![MonitorWin]::IsWindow($desk)){throw 'System panel exited unexpectedly'}
 [void][MonitorWin]::SetWindowPos($desk,[IntPtr](-1),0,0,0,0,0x13)
 Start-Sleep -Milliseconds 100
 $r=New-Object MonitorWin+RECT;[void][MonitorWin]::GetWindowRect($script:desk,[ref]$r)
 $bitmap=New-Object Drawing.Bitmap ($r.Right-$r.Left),($r.Bottom-$r.Top);$graphics=[Drawing.Graphics]::FromImage($bitmap)
 try{$dc=$graphics.GetHdc();$screen=[MonitorWin]::GetDC([IntPtr]::Zero)
  try{[void][MonitorWin]::BitBlt($dc,0,0,$bitmap.Width,$bitmap.Height,$screen,$r.Left,$r.Top,0x40CC0020)}
  finally{[void][MonitorWin]::ReleaseDC([IntPtr]::Zero,$screen);$graphics.ReleaseHdc($dc)}
  $bitmap.Save((Join-Path $root "output/$name.png"),[Drawing.Imaging.ImageFormat]::Png)
 }finally{$graphics.Dispose();$bitmap.Dispose();[void][MonitorWin]::SetWindowPos($desk,[IntPtr](-2),0,0,0,0,0x13)}
}
function ExportTable([string]$name){
 $destination=Join-Path $root "output/$name-$PID-$([DateTime]::Now.Ticks).csv"
 [void][MonitorWin]::PostMessage($script:desk,0x111,[IntPtr]3015,[IntPtr]::Zero)
 $dialog=[IntPtr]::Zero
 for($i=0;$i -lt 50;$i++){Start-Sleep -Milliseconds 100;$dialog=[MonitorWin]::FindWindow('#32770','另存为');if($dialog -ne [IntPtr]::Zero){break}}
 if($dialog -eq [IntPtr]::Zero){throw 'CSV save dialog did not open'}
 $fileEdit=[IntPtr]::Zero
 for($i=0;$i -lt 30;$i++){Start-Sleep -Milliseconds 100;$fileEdit=[MonitorWin]::FindFilename($dialog);if($fileEdit -ne [IntPtr]::Zero){break}}
 if($fileEdit -eq [IntPtr]::Zero){throw 'Save filename edit not found'}
 # Replace the selection so the shell receives the edit-change notification.
 # WM_SETTEXT alone can leave its cached filename pointing at the previous export.
 [void][MonitorWin]::SendMessage($fileEdit,0xB1,[IntPtr]::Zero,[IntPtr](-1))
 [void][MonitorWin]::SendText($fileEdit,0xC2,[IntPtr]1,$destination)
 Start-Sleep -Milliseconds 250
 [void][MonitorWin]::PostMessage([MonitorWin]::GetDlgItem($dialog,1),0xF5,[IntPtr]::Zero,[IntPtr]::Zero)
 for($i=0;$i -lt 50;$i++){Start-Sleep -Milliseconds 100;if(Test-Path -LiteralPath $destination){return @(Import-Csv -LiteralPath $destination)}}
 throw "CSV was not exported: $name"
}

}
