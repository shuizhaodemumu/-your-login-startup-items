using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace DragonGuard {
 static class Theme {
  public static Color Purple=Color.FromArgb(124,76,213),Ink=Color.FromArgb(48,36,68),Muted=Color.FromArgb(126,115,144),Bg=Color.FromArgb(247,245,252),Line=Color.FromArgb(230,222,242),Dark=Color.FromArgb(36,26,54);
  public static Font Font(float size=10,FontStyle style=FontStyle.Regular){return new Font("Microsoft YaHei UI",size,style);}
  public static Label Label(string text,float size,Color color){return new Label{Text=text,Font=Font(size),ForeColor=color,BackColor=Color.Transparent,AutoEllipsis=true};}
  public static Button Button(string text, bool primary=false){return new Button{Text=text,FlatStyle=FlatStyle.Flat,BackColor=primary?Purple:Color.White,ForeColor=primary?Color.White:Purple,Font=Font(9),Height=36,Width=100,Cursor=Cursors.Hand,FlatAppearance={BorderColor=Line,BorderSize=primary?0:1}};}
  public static Image Asset(string name){using(var s=Assembly.GetExecutingAssembly().GetManifestResourceStream(name)){return new Bitmap(s);}}
 }
 class RoundPanel:Panel {
  public Color Fill=Color.White,Border=Theme.Line;public bool Dashed;
  public RoundPanel(){DoubleBuffered=true;BackColor=Color.Transparent;Padding=new Padding(18);}
  protected override void OnPaint(PaintEventArgs e){base.OnPaint(e);e.Graphics.SmoothingMode=SmoothingMode.AntiAlias;var r=new Rectangle(1,1,Width-3,Height-3);int d=22;
   using(var p=new GraphicsPath()){p.AddArc(r.Left,r.Top,d,d,180,90);p.AddArc(r.Right-d,r.Top,d,d,270,90);p.AddArc(r.Right-d,r.Bottom-d,d,d,0,90);p.AddArc(r.Left,r.Bottom-d,d,d,90,90);p.CloseFigure();using(var b=new SolidBrush(Fill))e.Graphics.FillPath(b,p);using(var pen=new Pen(Border,1)){if(Dashed)pen.DashStyle=DashStyle.Dash;e.Graphics.DrawPath(pen,p);}}
  }
 }
 sealed class RuntimeEntry:IDisposable {
  public AppEntry Config;public Snapshot Last;public IntPtr Handle;public bool Busy,Disposed;
  public AppCard Card;
  public RuntimeEntry(AppEntry e){Config=e;Handle=Native.dg_create(e.Path,e.Arguments,e.Directory);Native.dg_enable(Handle,e.Watch?1:0);if(e.HasPlacement){var p=e.GetPlacement();Native.dg_restore(Handle,ref p);}}
  public void Dispose(){if(!Disposed){Disposed=true;Native.dg_free(Handle);Handle=IntPtr.Zero;}}
 }
 sealed class AppCard:RoundPanel {
  public readonly RuntimeEntry Runtime;
  Label state,detail,name;CheckBox watch,auto;Button start,restart,edit,remove;
  readonly MainWindow owner;bool init=true;
  public AppCard(MainWindow window,RuntimeEntry runtime){owner=window;Runtime=runtime;Height=151;Margin=new Padding(0,0,0,12);Padding=new Padding(16);
   name=Theme.Label(runtime.Config.Name,12,Theme.Ink);name.Font=Theme.Font(12,FontStyle.Bold);name.SetBounds(60,17,350,26);Controls.Add(name);
   var icon=new PictureBox{SizeMode=PictureBoxSizeMode.Zoom,Bounds=new Rectangle(18,18,30,30)};try{using(var i=Icon.ExtractAssociatedIcon(runtime.Config.Path)){icon.Image=i.ToBitmap();}}catch{}Controls.Add(icon);
   state=Theme.Label("尚未启动",9,Theme.Muted);state.TextAlign=ContentAlignment.MiddleRight;Controls.Add(state);
   detail=Theme.Label(runtime.Config.Path,8.5f,Theme.Muted);detail.SetBounds(60,48,450,21);Controls.Add(detail);
   auto=new CheckBox{Text="登录自启",Checked=runtime.Config.AutoStart,AutoSize=true,ForeColor=Theme.Ink,Font=Theme.Font(9),Location=new System.Drawing.Point(20,81)};
   watch=new CheckBox{Text="看门狗 · 10 秒",Checked=runtime.Config.Watch,AutoSize=true,ForeColor=Theme.Ink,Font=Theme.Font(9),Location=new System.Drawing.Point(132,81)};
   Controls.Add(auto);Controls.Add(watch);
   auto.CheckedChanged+=(s,e)=>{if(init)return;runtime.Config.AutoStart=auto.Checked;owner.SaveSettings();};
   watch.CheckedChanged+=async(s,e)=>{if(init)return;bool enabled=watch.Checked;runtime.Config.Watch=enabled;owner.SaveSettings();await owner.Run(runtime,()=>{Native.dg_enable(runtime.Handle,enabled?1:0);return 0;});};
   start=Theme.Button("启动 / 继续",true);restart=Theme.Button("重启");edit=Theme.Button("设置");remove=Theme.Button("移除");
   start.Click+=async(s,e)=>await owner.Run(runtime,()=>Native.dg_start(runtime.Handle));
   restart.Click+=async(s,e)=>await owner.Run(runtime,()=>Native.dg_restart(runtime.Handle));
   edit.Click+=(s,e)=>owner.Edit(runtime);remove.Click+=(s,e)=>owner.Remove(runtime);
   foreach(var b in new[]{start,restart,edit,remove})Controls.Add(b);
   init=false;Resize+=(s,e)=>Arrange();Arrange();
  }
  void Arrange(){name.Width=Math.Max(120,Width-255);state.SetBounds(Math.Max(215,Width-240),18,220,24);detail.Width=Width-80;int x=Width-364;start.SetBounds(x,104,116,32);restart.SetBounds(x+124,104,70,32);edit.SetBounds(x+202,104,70,32);remove.SetBounds(x+280,104,70,32);}
  public void UpdateState(){var s=Runtime.Last;state.ForeColor=s.State==5?Color.FromArgb(186,75,92):s.State==2?Color.DarkOrange:Theme.Purple;
   string t=s.State==1?"● 正在守护":s.State==2?"无响应 "+(s.HungMs/1000.0).ToString("0.0")+" / 10 秒":s.State==4?"运行中 · 守护已暂停":s.State==5?"需要处理":"尚未启动";
   if(Runtime.Busy)t="正在处理…";state.Text=t;
   detail.Text=s.State==5?Native.ErrorText(s.Error):(s.Pid!=0?"PID "+s.Pid+"  ·  已重启 "+s.Restarts+" 次  ·  ":"")+Runtime.Config.Path;
   foreach(var c in new Control[]{start,restart,edit,remove,watch})c.Enabled=!Runtime.Busy;
  }
  protected override void Dispose(bool disposing){if(disposing){foreach(var p in Controls.OfType<PictureBox>())if(p.Image!=null)p.Image.Dispose();}base.Dispose(disposing);}
 }
 sealed class MainWindow:Form {
  readonly Storage storage;readonly Settings settings;readonly bool preview,startup;
  readonly List<RuntimeEntry> entries=new List<RuntimeEntry>();
  readonly System.Windows.Forms.Timer timer=new System.Windows.Forms.Timer{Interval=1000};
  readonly NotifyIcon tray=new NotifyIcon();readonly Image welcome,guard,recover;
  readonly PictureBox mascot=new PictureBox{SizeMode=PictureBoxSizeMode.Zoom,BackColor=Color.Transparent};
  readonly FlowLayoutPanel list=new FlowLayoutPanel{Dock=DockStyle.Fill,FlowDirection=FlowDirection.TopDown,WrapContents=false,AutoScroll=true,BackColor=Theme.Bg,Margin=new Padding(0)};
  readonly Label registered=Theme.Label("0",25,Theme.Ink),watching=Theme.Label("0",25,Theme.Purple),restarted=Theme.Label("0",25,Theme.Ink),speech=Theme.Label("把常用程序交给我吧。",11,Color.White);
  readonly Label empty=Theme.Label("还没有添加程序\n\n把桌面程序图标拖到上方，开始第一次守护。",11,Theme.Muted);
  readonly Label notice=Theme.Label("准备就绪。拖入程序后会立即开始守护。",9,Theme.Muted);
  readonly CheckBox login=new CheckBox{Text="登录后自动守护",AutoSize=true,Font=Theme.Font(9),ForeColor=Theme.Ink};
  bool quitting,loading=true,saveFailed,hiddenHint;int ticks;
  public MainWindow(Storage store,bool isStartup,bool isPreview){storage=store;preview=isPreview;startup=isStartup;settings=preview?new Settings():store.Load();
   Text="玲珑守护 · 程序自启与看门狗";ClientSize=new Size(1140,760);MinimumSize=new Size(1050,720);StartPosition=FormStartPosition.CenterScreen;BackColor=Theme.Bg;Font=Theme.Font();AutoScaleMode=AutoScaleMode.Dpi;
   Icon=Icon.ExtractAssociatedIcon(Application.ExecutablePath);DoubleBuffered=true;
   welcome=Theme.Asset("welcome.png");guard=Theme.Asset("guard.png");recover=Theme.Asset("recover.png");mascot.Image=welcome;
   var shell=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=2,RowCount=1,Margin=new Padding(0),Padding=new Padding(0)};shell.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute,278));shell.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,100));Controls.Add(shell);
   var side=new Panel{Dock=DockStyle.Fill,BackColor=Theme.Dark,Margin=new Padding(0)};shell.Controls.Add(side,0,0);
   var brand=Theme.Label("玲珑守护",22,Color.White);brand.Font=Theme.Font(22,FontStyle.Bold);brand.SetBounds(28,34,224,40);side.Controls.Add(brand);
   var sub=Theme.Label("LINGLONG GUARD  /  01",8.5f,Color.FromArgb(189,168,221));sub.SetBounds(30,82,220,25);side.Controls.Add(sub);
   var nav=Theme.Label("◈   程序守护台",11,Color.FromArgb(225,208,255));nav.SetBounds(28,143,222,40);side.Controls.Add(nav);
   mascot.SetBounds(4,220,270,310);side.Controls.Add(mascot);
   speech.SetBounds(24,550,230,52);speech.TextAlign=ContentAlignment.MiddleCenter;side.Controls.Add(speech);
   var sideFoot=Theme.Label("关闭窗口后，我会留在托盘继续守护。\n要结束守护，请从托盘菜单退出。",8.5f,Color.FromArgb(185,169,208));sideFoot.SetBounds(24,660,232,55);sideFoot.Anchor=AnchorStyles.Bottom|AnchorStyles.Left;side.Controls.Add(sideFoot);
   var main=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=1,RowCount=6,Padding=new Padding(30,26,30,20),Margin=new Padding(0)};
   main.RowStyles.Add(new RowStyle(SizeType.Absolute,92));main.RowStyles.Add(new RowStyle(SizeType.Absolute,106));main.RowStyles.Add(new RowStyle(SizeType.Absolute,124));main.RowStyles.Add(new RowStyle(SizeType.Absolute,43));main.RowStyles.Add(new RowStyle(SizeType.Percent,100));main.RowStyles.Add(new RowStyle(SizeType.Absolute,43));shell.Controls.Add(main,1,0);
   var heading=new Panel{Dock=DockStyle.Fill};main.Controls.Add(heading,0,0);var title=Theme.Label("让常用程序，安心运行。",21,Theme.Ink);title.Font=Theme.Font(21,FontStyle.Bold);title.SetBounds(0,0,680,43);heading.Controls.Add(title);var intro=Theme.Label("登录自动开启  /  意外退出拉起  /  连续无响应 10 秒重启",9,Theme.Muted);intro.SetBounds(2,51,680,24);heading.Controls.Add(intro);
   var stats=new TableLayoutPanel{Dock=DockStyle.Fill,ColumnCount=3,RowCount=1,Margin=new Padding(0)};for(int i=0;i<3;i++)stats.ColumnStyles.Add(new ColumnStyle(SizeType.Percent,33.333f));
   int n=0;foreach(var pair in new[]{Tuple.Create("已管理程序",registered),Tuple.Create("正在守护",watching),Tuple.Create("本次恢复",restarted)}){var p=new RoundPanel{Dock=DockStyle.Fill,Margin=new Padding(0,0,n==2?0:12,12)};var l=Theme.Label(pair.Item1,9,Theme.Muted);l.SetBounds(18,13,160,20);pair.Item2.SetBounds(17,37,150,43);p.Controls.Add(l);p.Controls.Add(pair.Item2);stats.Controls.Add(p,n++,0);}main.Controls.Add(stats,0,1);
   var drop=new RoundPanel{Dock=DockStyle.Fill,Fill=Color.FromArgb(242,235,255),Border=Color.FromArgb(180,146,232),Dashed=true,Margin=new Padding(0,0,0,14)};
   var dtitle=Theme.Label("＋  把程序图标拖到这里",14,Theme.Purple);dtitle.Font=Theme.Font(14,FontStyle.Bold);dtitle.SetBounds(22,21,450,30);drop.Controls.Add(dtitle);
   var dsub=Theme.Label("支持 .exe 和桌面快捷方式 .lnk，可一次拖入多个",9,Theme.Muted);dsub.SetBounds(24,59,460,22);drop.Controls.Add(dsub);
   var add=Theme.Button("选择程序",true);add.Anchor=AnchorStyles.Top|AnchorStyles.Right;drop.Controls.Add(add);drop.Resize+=(s,e)=>add.SetBounds(drop.Width-132,34,108,37);add.Click+=(s,e)=>Browse();main.Controls.Add(drop,0,2);
   var listHeading=new Panel{Dock=DockStyle.Fill};var lh=Theme.Label("我的程序",12,Theme.Ink);lh.Font=Theme.Font(12,FontStyle.Bold);lh.SetBounds(0,2,180,28);listHeading.Controls.Add(lh);login.Location=new System.Drawing.Point(440,5);login.Anchor=AnchorStyles.Top|AnchorStyles.Right;listHeading.Controls.Add(login);listHeading.Resize+=(s,e)=>login.Left=listHeading.Width-login.Width;main.Controls.Add(listHeading,0,3);
   empty.TextAlign=ContentAlignment.MiddleCenter;empty.Height=130;empty.Width=650;list.Controls.Add(empty);main.Controls.Add(list,0,4);
   var bottom=new Panel{Dock=DockStyle.Fill};notice.SetBounds(0,12,530,24);notice.Anchor=AnchorStyles.Top|AnchorStyles.Left|AnchorStyles.Right;bottom.Controls.Add(notice);var logs=Theme.Button("运行记录");logs.Width=95;logs.Anchor=AnchorStyles.Right|AnchorStyles.Top;bottom.Controls.Add(logs);bottom.Resize+=(s,e)=>{logs.Left=bottom.Width-95;notice.Width=bottom.Width-110;};logs.Click+=(s,e)=>ShowLogs();main.Controls.Add(bottom,0,5);
   list.SizeChanged+=(s,e)=>{foreach(Control c in list.Controls)c.Width=Math.Max(600,list.ClientSize.Width-SystemInformation.VerticalScrollBarWidth-4);};
   EnableDrop(this);foreach(var e in settings.Apps){try{AddRuntime(e);}catch(Exception ex){Record(e.Name+"："+ex.Message);}}
   login.Checked=!preview&&Startup.Enabled;login.Enabled=!preview;login.CheckedChanged+=(s,e)=>{if(loading)return;try{Startup.Set(login.Checked);Record(login.Checked?"已开启登录自动守护":"已关闭登录自动守护");}catch(Exception ex){loading=true;login.Checked=Startup.Enabled;loading=false;MessageBox.Show(this,ex.Message,"无法修改自启");}};
   var menu=new ContextMenuStrip();menu.Items.Add("打开守护台",null,(s,e)=>Reveal());menu.Items.Add("暂停全部看门狗",null,async(s,e)=>{foreach(var r in entries.ToArray()){r.Config.Watch=false;await Run(r,()=>{Native.dg_enable(r.Handle,0);return 0;});}RebuildCards();SaveSettings();});menu.Items.Add("退出守护（保留目标程序运行）",null,(s,e)=>Quit());tray.Icon=Icon;tray.Text="玲珑守护";tray.ContextMenuStrip=menu;tray.DoubleClick+=(s,e)=>Reveal();tray.Visible=!preview;
   timer.Tick+=(s,e)=>{if(quitting)return;ticks++;foreach(var r in entries.ToArray())if(!r.Busy)StartTick(r);RefreshSummary();if(ticks%15==0)SaveSettings();};
   Shown+=async(s,e)=>{loading=false;if(preview)return;if(startup)Hide();timer.Start();foreach(var r in entries.ToArray()){if(r.Config.AutoStart)await Run(r,()=>Native.dg_start(r.Handle));}if(storage.Warning!=null)MessageBox.Show(this,storage.Warning,"配置恢复");};
   FormClosing+=(s,e)=>{if(e.CloseReason==CloseReason.UserClosing&&!quitting&&!preview){e.Cancel=true;Hide();if(!hiddenHint){tray.ShowBalloonTip(2500,"玲珑守护","已收起到托盘，守护仍在继续。",ToolTipIcon.Info);hiddenHint=true;}}else {quitting=true;timer.Stop();SaveSettings();}};
   FormClosed+=(s,e)=>{tray.Visible=false;tray.Dispose();foreach(var r in entries)if(!r.Busy)r.Dispose();welcome.Dispose();guard.Dispose();recover.Dispose();};RefreshSummary();
  }
  void EnableDrop(Control c){c.AllowDrop=true;c.DragEnter+=OnDragOverFiles;c.DragOver+=OnDragOverFiles;c.DragDrop+=OnDrop;foreach(Control child in c.Controls)EnableDrop(child);}
  void OnDragOverFiles(object sender,DragEventArgs e){e.Effect=e.Data.GetDataPresent(DataFormats.FileDrop)?DragDropEffects.Copy:DragDropEffects.None;}
  async void OnDrop(object sender,DragEventArgs e){if(e.Data.GetDataPresent(DataFormats.FileDrop))await AddFiles((string[])e.Data.GetData(DataFormats.FileDrop));}
  async void Browse(){using(var d=new OpenFileDialog{Title="选择需要守护的程序",Filter="桌面程序与快捷方式|*.exe;*.lnk",Multiselect=true,DereferenceLinks=false}){if(d.ShowDialog(this)==DialogResult.OK)await AddFiles(d.FileNames);}}
  public async Task AddFiles(string[] files){var errors=new List<string>();foreach(string f in files){try{var e=Importer.Resolve(f);if(entries.Any(existing=>string.Equals(existing.Config.Path,e.Path,StringComparison.OrdinalIgnoreCase))){errors.Add(e.Name+"：已在管理列表中");continue;}settings.Apps.Add(e);var r=AddRuntime(e);SaveSettings();Record("已添加 "+e.Name);await Run(r,()=>Native.dg_start(r.Handle));}catch(Exception ex){errors.Add(System.IO.Path.GetFileName(f)+"："+ex.Message);}}
   if(errors.Count>0)MessageBox.Show(this,string.Join("\n\n",errors),"添加结果");if(entries.Count>0&&!login.Checked)notice.Text="已开始守护；勾选「登录后自动守护」可随 Windows 启动。";RefreshSummary();}
  RuntimeEntry AddRuntime(AppEntry e){var r=new RuntimeEntry(e);entries.Add(r);r.Card=new AppCard(this,r);EnableDrop(r.Card);list.Controls.Add(r.Card);r.Card.Width=Math.Max(600,list.ClientSize.Width-SystemInformation.VerticalScrollBarWidth-4);return r;}
  void RebuildCards(){foreach(var r in entries){list.Controls.Remove(r.Card);r.Card.Dispose();r.Card=new AppCard(this,r);EnableDrop(r.Card);list.Controls.Add(r.Card);r.Card.Width=Math.Max(600,list.ClientSize.Width-SystemInformation.VerticalScrollBarWidth-4);r.Card.UpdateState();}}
  public async Task Run(RuntimeEntry r,Func<uint> action){while(r.Busy&&!quitting)await Task.Delay(40);if(r.Disposed||quitting)return;r.Busy=true;r.Card.UpdateState();try{uint result=await Task.Run(action);Snapshot snap;Native.dg_snapshot(r.Handle,out snap);Accept(r,snap);if(result!=0)Record(r.Config.Name+"："+Native.ErrorText(result));}catch(Exception ex){Record(r.Config.Name+"："+ex.Message);}finally{r.Busy=false;if(!quitting)r.Card.UpdateState();}RefreshSummary();SaveSettings();}
  async void StartTick(RuntimeEntry r){await Tick(r);}
  async Task Tick(RuntimeEntry r){if(r.Busy||r.Disposed||quitting)return;r.Busy=true;try{var snap=await Task.Run(()=>{Snapshot s;Native.dg_tick(r.Handle,out s);return s;});if(!quitting)Accept(r,snap);}catch(Exception ex){Record(r.Config.Name+"："+ex.Message);}finally{r.Busy=false;if(!quitting)r.Card.UpdateState();}}
  void Accept(RuntimeEntry r,Snapshot snap){var prev=r.Last;r.Last=snap;if(snap.HasPlacement!=0)r.Config.SetPlacement(snap.Placement);if(snap.Restarts>prev.Restarts)Record(r.Config.Name+"："+(snap.Reason==4?"连续无响应达到 10 秒，已结束旧进程并重启":snap.Reason==3?"检测到进程退出，已重新启动":"已手动重启"));if(snap.State==5&&prev.State!=5){Record(r.Config.Name+"："+Native.ErrorText(snap.Error));if(!preview)tray.ShowBalloonTip(4000,"守护需要处理",r.Config.Name+"："+Native.ErrorText(snap.Error),ToolTipIcon.Warning);}if(prev.Pid==0&&snap.Pid!=0)Record(r.Config.Name+(snap.Reason==2?"：已接管正在运行的实例":"：已启动"));r.Card.UpdateState();}
  void RefreshSummary(){registered.Text=entries.Count.ToString();watching.Text=entries.Count(r=>r.Config.Watch&&(r.Last.State==1||r.Last.State==2)).ToString();restarted.Text=entries.Sum(r=>(long)r.Last.Restarts).ToString();empty.Visible=entries.Count==0;bool needs=entries.Any(r=>r.Last.State==2||r.Last.State==5);mascot.Image=needs?recover:entries.Any(r=>r.Last.State==1)?guard:welcome;speech.Text=needs?"我发现了一点状况，\n请查看程序状态。":entries.Any(r=>r.Last.State==1)?"交给我吧，\n我会留意它们的状态。":"把常用程序交给我吧。";}
  public void SaveSettings(){if(preview)return;try{storage.Save(settings);saveFailed=false;}catch(Exception ex){if(!saveFailed){saveFailed=true;Record("配置保存失败："+ex.Message);notice.Text="配置保存失败，请检查数据目录权限。";}}}
  void Record(string message){if(IsDisposed)return;storage.Log(message);notice.Text=message;}
  public void Remove(RuntimeEntry r){if(r.Busy)return;entries.Remove(r);settings.Apps.Remove(r.Config);list.Controls.Remove(r.Card);r.Card.Dispose();r.Dispose();SaveSettings();Record("已移除 "+r.Config.Name+"，目标程序继续运行");RefreshSummary();}
  public void Edit(RuntimeEntry r){if(r.Busy)return;using(var d=new EditDialog(r.Config)){if(d.ShowDialog(this)!=DialogResult.OK)return;r.Config.Name=d.AppName;r.Config.Arguments=d.Arguments;r.Config.Directory=d.WorkingDirectory;Native.dg_configure(r.Handle,d.Arguments,d.WorkingDirectory);RebuildCards();SaveSettings();Record("设置已保存，新参数在下次重启时生效。");}}
  void ShowLogs(){var f=new Form{Text="玲珑守护 · 运行记录",Size=new Size(820,560),StartPosition=FormStartPosition.CenterParent,Icon=Icon};var text=new TextBox{Multiline=true,ReadOnly=true,ScrollBars=ScrollBars.Both,Dock=DockStyle.Fill,Font=Theme.Font(9),BackColor=Color.White};string path=System.IO.Path.Combine(storage.Root,"events.log");text.Text=File.Exists(path)?File.ReadAllText(path):"还没有运行记录。";f.Controls.Add(text);f.ShowDialog(this);f.Dispose();}
  public void Reveal(){Show();WindowState=FormWindowState.Normal;Activate();}
  async void Quit(){if(quitting)return;quitting=true;timer.Stop();Enabled=false;while(entries.Any(r=>r.Busy))await Task.Delay(50);SaveSettings();Close();}
  public void SavePreview(string path){using(var bitmap=new Bitmap(Width,Height)){DrawToBitmap(bitmap,new Rectangle(0,0,Width,Height));bitmap.Save(path,System.Drawing.Imaging.ImageFormat.Png);}}
 }
 sealed class EditDialog:Form {
  readonly TextBox name=new TextBox(),args=new TextBox(),dir=new TextBox();
  public string AppName{get{return name.Text.Trim();}}public string Arguments{get{return args.Text;}}public string WorkingDirectory{get{return dir.Text.Trim();}}
  public EditDialog(AppEntry entry){Text="程序设置 · "+entry.Name;ClientSize=new Size(600,330);FormBorderStyle=FormBorderStyle.FixedDialog;MaximizeBox=false;MinimizeBox=false;StartPosition=FormStartPosition.CenterParent;BackColor=Theme.Bg;Font=Theme.Font(9);
   int y=23;foreach(var pair in new[]{Tuple.Create("显示名称",name,entry.Name),Tuple.Create("启动参数",args,entry.Arguments),Tuple.Create("工作目录",dir,entry.Directory)}){var l=Theme.Label(pair.Item1,9,Theme.Muted);l.SetBounds(24,y,550,24);Controls.Add(l);pair.Item2.SetBounds(24,y+26,550,28);pair.Item2.Text=pair.Item3;Controls.Add(pair.Item2);y+=72;}
   var info=Theme.Label("自动恢复启动参数、工作目录和主窗口位置；未保存内容由目标程序恢复。",8.5f,Theme.Muted);info.SetBounds(24,242,550,25);Controls.Add(info);var save=Theme.Button("保存",true);save.SetBounds(474,280,100,35);Controls.Add(save);save.Click+=(s,e)=>{if(string.IsNullOrWhiteSpace(AppName)||!Directory.Exists(WorkingDirectory)){MessageBox.Show(this,"请输入名称，并选择存在的工作目录。");return;}DialogResult=DialogResult.OK;};AcceptButton=save;}
 }
 static class Program {
  [DllImport("user32.dll")]static extern bool SetProcessDPIAware();
  [STAThread] static void Main(string[] args){
   SetProcessDPIAware();Application.EnableVisualStyles();Application.SetCompatibleTextRenderingDefault(false);
   bool preview=args.Contains("--preview"),startup=args.Contains("--startup");
   string data=System.IO.Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"LinglongGuard");
   int i=Array.IndexOf(args,"--data-dir");if(i>=0&&i+1<args.Length)data=System.IO.Path.GetFullPath(args[i+1]);
   Mutex mutex=null;EventWaitHandle signal=null;RegisteredWaitHandle wait=null;
   try {
    if(!preview){bool created;string suffix=System.Security.Principal.WindowsIdentity.GetCurrent().User.Value;mutex=new Mutex(true,"Local\\LinglongGuard-"+suffix,out created);signal=new EventWaitHandle(false,EventResetMode.AutoReset,"Local\\LinglongGuard-Show-"+suffix);if(!created){signal.Set();return;}}
    var f=new MainWindow(new Storage(data),startup,preview);
    if(signal!=null)wait=ThreadPool.RegisterWaitForSingleObject(signal,(o,t)=>{if(f.IsHandleCreated&&!f.IsDisposed)f.BeginInvoke((Action)f.Reveal);},null,-1,false);
    int capture=Array.IndexOf(args,"--capture");if(preview&&capture>=0&&capture+1<args.Length){f.Shown+=(s,e)=>{var timer=new System.Windows.Forms.Timer{Interval=800};timer.Tick+=(a,b)=>{timer.Stop();f.SavePreview(args[capture+1]);f.Close();};timer.Start();};}
    Application.Run(f);
   }catch(Exception ex){MessageBox.Show(ex.Message,"玲珑守护无法启动",MessageBoxButtons.OK,MessageBoxIcon.Error);}
   finally {if(wait!=null)wait.Unregister(null);if(signal!=null)signal.Dispose();if(mutex!=null)mutex.Dispose();}
  }
 }
}
