using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using DragonGuard;

class Integration {
 [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h,uint msg,IntPtr w,IntPtr l);
 [DllImport("user32.dll")] static extern bool MoveWindow(IntPtr h,int x,int y,int width,int height,bool repaint);
 [DllImport("user32.dll")] static extern bool GetWindowPlacement(IntPtr h,ref Placement p);
 [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h,int command);
 delegate bool EnumProc(IntPtr hwnd,IntPtr data);
 [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc proc,IntPtr data);
 [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr hwnd);
 [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr hwnd,out uint pid);
 static string exe,work,marker;static IntPtr guard;static int count;
 static Snapshot Snap(){Snapshot s;Native.dg_snapshot(guard,out s);return s;}
 static Snapshot Tick(){Snapshot s;Native.dg_tick(guard,out s);return s;}
 static void Assert(bool good,string message){if(!good)throw new Exception("FAIL: "+message);Console.WriteLine("PASS: "+message);count++;}
 static bool Alive(uint pid){try{using(var p=Process.GetProcessById((int)pid)){return !p.HasExited;}}catch(ArgumentException){return false;}}
 static IntPtr Window(uint pid){for(int i=0;i<50;i++){IntPtr found=IntPtr.Zero;EnumWindows((h,d)=>{uint p;GetWindowThreadProcessId(h,out p);if(p==pid&&IsWindowVisible(h)){found=h;return false;}return true;},IntPtr.Zero);if(found!=IntPtr.Zero)return found;Thread.Sleep(100);}throw new Exception("Fixture window not ready");}
 static Snapshot Until(Func<Snapshot,bool> condition,int seconds){var clock=Stopwatch.StartNew();Snapshot s=Snap();while(clock.Elapsed.TotalSeconds<seconds){s=Tick();if(condition(s))return s;Thread.Sleep(250);}throw new Exception("Timed out: state="+s.State+" error="+s.Error+" pid="+s.Pid+" restarts="+s.Restarts);}
 static void New(string args){guard=Native.dg_create(exe,args,work);}
 static void Cleanup(){if(guard!=IntPtr.Zero){Native.dg_enable(guard,0);Native.dg_free(guard);guard=IntPtr.Zero;}
  // Only this fixture's exact executable path can be terminated during cleanup.
  foreach(var p in Process.GetProcessesByName(Path.GetFileNameWithoutExtension(exe))){using(p){try{if(string.Equals(p.MainModule.FileName,exe,StringComparison.OrdinalIgnoreCase)){p.Kill();p.WaitForExit(3000);}}catch(InvalidOperationException){}}}
 }
 [STAThread] static int Main(){
  exe=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"DragonGuardFixture.exe");work=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"test data 空格");Directory.CreateDirectory(work);marker=Path.Combine(work,"launches-"+Guid.NewGuid().ToString("N")+".log");Environment.SetEnvironmentVariable("DRAGON_TEST_MARKER",marker);
  try {
   Assert(Marshal.SizeOf(typeof(Snapshot))==72,"FFI snapshot layout is 72 bytes");
   var entry=Importer.Resolve(exe);Assert(entry.Path==exe&&entry.Directory==Path.GetDirectoryName(exe),"EXE import resolves path and working directory");
   string shortcut=Path.Combine(work,"中文 图标.lnk");var type=Type.GetTypeFromProgID("WScript.Shell");object shell=Activator.CreateInstance(type),lnk=type.InvokeMember("CreateShortcut",System.Reflection.BindingFlags.InvokeMethod,null,shell,new object[]{shortcut});
   Action<string,object> set=(name,value)=>lnk.GetType().InvokeMember(name,System.Reflection.BindingFlags.SetProperty,null,lnk,new object[]{value});set("TargetPath",exe);set("Arguments","--label \"中文 参数\"");set("WorkingDirectory",work);lnk.GetType().InvokeMember("Save",System.Reflection.BindingFlags.InvokeMethod,null,lnk,null);Marshal.FinalReleaseComObject(lnk);Marshal.FinalReleaseComObject(shell);
   entry=Importer.Resolve(shortcut);Assert(entry.Arguments=="--label \"中文 参数\""&&entry.Directory==work,"LNK import preserves quoted Unicode arguments and working directory");
   bool rejected=false;try{Importer.Resolve(marker);}catch{rejected=true;}Assert(rejected,"Unsupported or nonexistent input rejected");
   var storage=new Storage(Path.Combine(work,"config"));var settings=new Settings();settings.Apps.Add(entry);storage.Save(settings);settings.Apps[0].Name="roundtrip";storage.Save(settings);Assert(storage.Load().Apps[0].Arguments==entry.Arguments,"Atomic settings round-trip preserves arguments");File.WriteAllText(Path.Combine(storage.Root,"settings.json"),"broken");Assert(storage.Load().Apps.Count==1&&storage.Warning!=null,"Corrupt settings recover from retained backup");
   New("--label \"中文 参数\" --child");Assert(Native.dg_start(guard)==0,"Launch native desktop fixture");var first=Snap();var hwnd=Window(first.Pid);Until(s=>s.HasPlacement==1,5);
   Assert(File.ReadAllText(marker).Contains(work+"|--label~中文 参数~--child"),"Actual process receives exact working directory and argument token");
   MoveWindow(hwnd,220,180,500,330,true);Thread.Sleep(300);var captured=Tick();Assert(captured.Placement.Rect.Left==220&&captured.Placement.Rect.Right-captured.Placement.Rect.Left==500,"Window position and dimensions captured");
   uint child=uint.Parse(File.ReadAllLines(marker+".child").Last());PostMessage(hwnd,0x10,IntPtr.Zero,IntPtr.Zero);var reopened=Until(s=>s.Restarts==1&&s.Pid!=first.Pid,8);Assert(!Alive(first.Pid)&&!Alive(child),"Exit recovery removes old owned child process before restart");var hwnd2=Window(reopened.Pid);Until(s=>s.State==1,5);var placement=new Placement{Length=44};GetWindowPlacement(hwnd2,ref placement);Assert(placement.Rect.Left==220&&placement.Rect.Top==180&&placement.Rect.Right-placement.Rect.Left==500,"Restart restores actual window placement");
   ShowWindow(hwnd2,3);Thread.Sleep(300);Assert(Tick().Placement.Show==3,"Maximized state captured");Native.dg_restart(guard);reopened=Snap();hwnd2=Window(reopened.Pid);Until(s=>s.State==1,5);placement=new Placement{Length=44};GetWindowPlacement(hwnd2,ref placement);Assert(placement.Show==3,"Maximized state restored after manual restart");
   ShowWindow(hwnd2,6);Thread.Sleep(300);Assert(Tick().Placement.Show==2,"Minimized state captured");Native.dg_restart(guard);reopened=Snap();hwnd2=Window(reopened.Pid);Until(s=>s.State==1,5);placement=new Placement{Length=44};GetWindowPlacement(hwnd2,ref placement);Assert(placement.Show==2,"Minimized state restored after manual restart");ShowWindow(hwnd2,9);Thread.Sleep(300);Tick();
   uint before=Snap().Restarts;PostMessage(hwnd2,0x8001,new IntPtr(3500),IntPtr.Zero);var shortHang=Stopwatch.StartNew();bool observed=false;while(shortHang.Elapsed.TotalSeconds<5){var s=Tick();observed|=s.State==2;Thread.Sleep(250);}Assert(observed&&Snap().Restarts==before&&Snap().State==1,"Transient unresponsiveness below 10 seconds does not restart");
   var watch=Stopwatch.StartNew();PostMessage(hwnd2,0x8001,new IntPtr(30000),IntPtr.Zero);var afterHang=Until(s=>s.Restarts>before,18);Assert(watch.Elapsed.TotalSeconds>=10&&watch.Elapsed.TotalSeconds<16&&!Alive(reopened.Pid),"Sustained hang killed and restarted after >=10 seconds ("+watch.Elapsed.TotalSeconds.ToString("0.00")+"s)");
   Window(afterHang.Pid);Until(s=>s.State==1,5);Native.dg_enable(guard,0);Assert(Snap().State==4&&Snap().HungMs==0,"Pause state is immediately reflected in UI snapshot");PostMessage(Window(afterHang.Pid),0x10,IntPtr.Zero,IntPtr.Zero);Thread.Sleep(600);var paused=Tick();Assert(paused.State==0&&paused.Restarts==afterHang.Restarts,"Paused watchdog does not resurrect a deliberately closed program");Cleanup();
   New("--worker");Assert(Native.dg_start(guard)==0,"Launch windowless fixture");var background=Tick();Assert(background.State==1&&background.HasPlacement==0,"Windowless process is monitored for liveness only");using(var p=Process.GetProcessById((int)background.Pid)){p.Kill();p.WaitForExit();}var bgRestart=Until(s=>s.Restarts==1,5);Assert(bgRestart.Pid!=background.Pid,"Windowless process exit triggers restart");Cleanup();
   New("--exit");Assert(Native.dg_start(guard)==0,"Start crash-loop fixture");var stopped=Until(s=>s.State==5,10);Assert(stopped.Error==100002&&stopped.Restarts==5,"Five restarts per minute trip circuit breaker");Cleanup();
   New("");var external=Process.Start(new ProcessStartInfo(exe){UseShellExecute=false});Window((uint)external.Id);Assert(Native.dg_start(guard)==0&&Snap().Pid==external.Id&&Snap().Reason==2,"Existing single instance is adopted without duplication");Native.dg_free(guard);guard=IntPtr.Zero;Assert(!external.HasExited,"Releasing watchdog leaves application alive");external.Kill();external.WaitForExit();external.Dispose();
   var a=Process.Start(new ProcessStartInfo(exe){UseShellExecute=false});var b=Process.Start(new ProcessStartInfo(exe){UseShellExecute=false});Window((uint)a.Id);Window((uint)b.Id);New("");Assert(Native.dg_start(guard)==100001&&!a.HasExited&&!b.HasExited,"Ambiguous independent instances are not randomly selected or terminated");Cleanup();a.Dispose();b.Dispose();
   guard=Native.dg_create(Path.Combine(work,"missing.exe"),"",work);Assert(Native.dg_start(guard)==2&&Snap().State==5,"Missing executable reports actionable blocked state");Cleanup();
   Console.WriteLine("RESULT: "+count+" integration assertions passed. No real user program, startup registry, logoff or reboot touched.");return 0;
  }catch(Exception ex){Console.WriteLine(ex.ToString());return 1;}finally{Cleanup();Environment.SetEnvironmentVariable("DRAGON_TEST_MARKER",null);}
 }
}
