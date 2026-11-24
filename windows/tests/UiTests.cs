using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Threading.Tasks;
using System.Windows.Forms;
using DragonGuard;

class UiTests {
 static int result=1,count;static string fixture;static List<Process> owned=new List<Process>();
 static void Assert(bool value,string text){if(!value)throw new Exception("FAIL: "+text);Console.WriteLine("PASS: "+text);count++;}
 static IEnumerable<Control> All(Control c){yield return c;foreach(Control child in c.Controls)foreach(var item in All(child))yield return item;}
 [STAThread] static int Main(){Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);fixture=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"DragonGuardFixture.exe");var data=Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"ui-test-data");var f=new MainWindow(new Storage(data),false,true);
  f.Shown+=async(sender,eventArgs)=>{try{
   Assert(f.Text.Contains("玲珑守护")&&!All(f).Any(c=>c.Text.Contains("紫龙守护")),"Window and visible text use Linglong branding");
   Assert(All(f).Where(c=>c is Panel||c is Label||c is Button).All(c=>c.AllowDrop),"Drop handlers registered on nested labels, cards and buttons");
   var drop=new DataObject(DataFormats.FileDrop,new string[]{fixture});var e=new DragEventArgs(drop,0,0,0,DragDropEffects.Copy,DragDropEffects.None);
   typeof(MainWindow).GetMethod("OnDragOverFiles",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(f,new object[]{f,e});Assert(e.Effect==DragDropEffects.Copy,"Windows FileDrop data accepted");
   typeof(MainWindow).GetMethod("OnDrop",BindingFlags.Instance|BindingFlags.NonPublic).Invoke(f,new object[]{f,e});
   var field=typeof(MainWindow).GetField("entries",BindingFlags.Instance|BindingFlags.NonPublic);var entries=(List<RuntimeEntry>)field.GetValue(f);
   for(int i=0;i<50&&(entries.Count==0||entries[0].Busy||entries[0].Last.Pid==0);i++)await Task.Delay(100);
   Assert(entries.Count==1&&entries[0].Last.Pid!=0,"Actual drop callback adds a program and starts native watchdog");var r=entries[0];owned.Add(Process.GetProcessById((int)r.Last.Pid));
   for(int i=0;i<20&&!r.Card.Visible;i++)await Task.Delay(50);
   Assert(r.Card.Visible&&r.Card.Bounds.Right<=r.Card.Parent.ClientSize.Width,"Program card visible and inside scroll area");
   var checkbox=r.Card.Controls.OfType<CheckBox>().First(c=>c.Text.Contains("看门狗"));checkbox.Checked=false;while(r.Busy)await Task.Delay(50);Assert(!r.Config.Watch,"Per-program watchdog switch updates configuration");
   f.SavePreview(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"ui-populated.png"));
   f.Remove(r);Assert(entries.Count==0&&!owned[0].HasExited,"Removing a card detaches monitoring without killing target");
   Assert(!File.Exists(Path.Combine(data,"settings.json")),"Preview QA leaves real settings and login startup untouched");
   Console.WriteLine("RESULT: "+count+" UI integration assertions passed; drag event dispatched using a Windows FileDrop DataObject (not physical mouse drag).");result=0;
  }catch(Exception ex){Console.WriteLine(ex.ToString());}finally{foreach(var p in owned){try{if(!p.HasExited&&string.Equals(p.MainModule.FileName,fixture,StringComparison.OrdinalIgnoreCase)){p.Kill();p.WaitForExit(3000);}}catch{}p.Dispose();}f.Close();}};
  Application.Run(f);
  if(result!=0)return result;
  var runtimeStore=new Storage(Path.Combine(AppDomain.CurrentDomain.BaseDirectory,"ui-tray-test-"+Guid.NewGuid().ToString("N")));
  var trayWindow=new MainWindow(runtimeStore,true,false);
  trayWindow.Shown+=async(sender,e)=>{try{
   await Task.Delay(150);
   Assert(!trayWindow.Visible,"Startup mode hides main window without a real logon");
   var tray=(NotifyIcon)typeof(MainWindow).GetField("tray",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(trayWindow);
   Assert(tray.Visible,"Real NotifyIcon created for background guarding");
   trayWindow.Reveal();Assert(trayWindow.Visible,"Tray reveal action restores main window");
   typeof(MainWindow).GetField("hiddenHint",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(trayWindow,true);
   trayWindow.Close();Assert(!trayWindow.IsDisposed&&!trayWindow.Visible&&tray.Visible,"Close-to-tray preserves running guard window and tray icon");
   trayWindow.Reveal();await trayWindow.AddFiles(new[]{fixture});
   var entries=(List<RuntimeEntry>)typeof(MainWindow).GetField("entries",BindingFlags.Instance|BindingFlags.NonPublic).GetValue(trayWindow);var r=entries[0];using(var p=Process.GetProcessById((int)r.Last.Pid)){
    tray.ContextMenuStrip.Items[1].PerformClick();for(int i=0;i<40&&(r.Busy||r.Config.Watch);i++)await Task.Delay(50);
    Assert(!r.Config.Watch&&r.Last.State==4,"Tray pause-all menu immediately pauses native guard and status");
    Assert(runtimeStore.Load().Apps.Count==1&&!runtimeStore.Load().Apps[0].Watch,"Real UI changes persist in isolated settings directory");
    tray.ContextMenuStrip.Items[2].PerformClick();for(int i=0;i<40&&!trayWindow.IsDisposed;i++)await Task.Delay(50);
    Assert(!p.HasExited,"Tray quit leaves target process alive");p.Kill();p.WaitForExit(3000);
   }
   Console.WriteLine("RESULT: "+count+" UI assertions passed, including real tray lifecycle and startup-argument path. No physical mouse interaction or actual logon was simulated.");
  }catch(Exception ex){result=1;Console.WriteLine(ex.ToString());typeof(MainWindow).GetField("quitting",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(trayWindow,true);trayWindow.Close();}};
  Application.Run(trayWindow);return result;
 }
}
