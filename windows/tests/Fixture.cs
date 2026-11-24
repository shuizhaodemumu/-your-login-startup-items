using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Threading;
using System.Windows.Forms;

sealed class Fixture:Form {
 protected override bool ShowWithoutActivation{get{return true;}}
 protected override void WndProc(ref Message m){if(m.Msg==0x8001){Thread.Sleep(m.WParam.ToInt32());m.Result=IntPtr.Zero;return;}base.WndProc(ref m);}
 [STAThread] static void Main(string[] args){
  if(args.Contains("--worker")){Thread.Sleep(60000);return;}
  if(args.Contains("--exit"))return;
  Application.EnableVisualStyles();var f=new Fixture{Text="DragonGuard test fixture",Bounds=new Rectangle(90,90,380,240),StartPosition=FormStartPosition.Manual,ShowInTaskbar=false,FormBorderStyle=FormBorderStyle.FixedToolWindow};
  f.Controls.Add(new Label{Text="隔离测试程序 / 自动关闭\n用于验证看门狗，不是用户程序",Dock=DockStyle.Fill,TextAlign=ContentAlignment.MiddleCenter});
  string marker=Environment.GetEnvironmentVariable("DRAGON_TEST_MARKER");
  if(!string.IsNullOrEmpty(marker))File.AppendAllText(marker,Process.GetCurrentProcess().Id+"|"+Environment.CurrentDirectory+"|"+string.Join("~",args)+Environment.NewLine);
  if(args.Contains("--child")){var child=Process.Start(new ProcessStartInfo(Application.ExecutablePath,"--worker"){UseShellExecute=false,CreateNoWindow=true});if(!string.IsNullOrEmpty(marker))File.AppendAllText(marker+".child",child.Id+Environment.NewLine);}
  Application.Run(f);
 }
}
