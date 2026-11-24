using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Runtime.Serialization;
using System.Runtime.Serialization.Json;
using System.Text;
using Microsoft.Win32;

namespace DragonGuard {
 [DataContract] public class AppEntry {
  [DataMember] public string Id=Guid.NewGuid().ToString("N");
  [DataMember] public string Name;
  [DataMember] public string Path;
  [DataMember] public string Arguments="";
  [DataMember] public string Directory;
  [DataMember] public bool AutoStart=true;
  [DataMember] public bool Watch=true;
  [DataMember] public bool HasPlacement;
  [DataMember] public int Left,Top,Right,Bottom;
  [DataMember] public uint Show=1,Flags;
  public Placement GetPlacement(){return new Placement{Length=44,Flags=Flags,Show=Show,Rect=new Rect{Left=Left,Top=Top,Right=Right,Bottom=Bottom}};}
  public void SetPlacement(Placement p){HasPlacement=true;Left=p.Rect.Left;Top=p.Rect.Top;Right=p.Rect.Right;Bottom=p.Rect.Bottom;Show=p.Show;Flags=p.Flags;}
 }
 [DataContract] public class Settings {
  [DataMember] public int Version=1;
  [DataMember] public List<AppEntry> Apps=new List<AppEntry>();
 }
 public sealed class Storage {
  public readonly string Root;
  public string Warning;
  public Storage(string root){Root=root;System.IO.Directory.CreateDirectory(root);}
  public Settings Load(){
   string path=System.IO.Path.Combine(Root,"settings.json");
   if(!File.Exists(path))return new Settings();
   try{return Read(path);}catch(Exception ex){
    string backup=path+".bak";
    try {var s=Read(backup);Warning="配置损坏，已读取上一次保存的配置。原文件保留。";File.Copy(path,path+".damaged-"+DateTime.Now.ToString("yyyyMMdd-HHmmss"));return s;}
    catch {throw new InvalidDataException("配置无法读取，已保留原文件，请检查："+path,ex);}
   }
  }
  static Settings Read(string path){using(var f=File.OpenRead(path)){var s=(Settings)new DataContractJsonSerializer(typeof(Settings)).ReadObject(f);if(s==null||s.Version!=1||s.Apps==null)throw new InvalidDataException("配置格式不支持");return s;}}
  public void Save(Settings s){
   string path=System.IO.Path.Combine(Root,"settings.json"),temp=path+".tmp";
   using(var f=new FileStream(temp,FileMode.Create,FileAccess.Write,FileShare.None)){new DataContractJsonSerializer(typeof(Settings)).WriteObject(f,s);f.Flush(true);}
   if(File.Exists(path))File.Replace(temp,path,path+".bak");else File.Move(temp,path);
  }
  public void Log(string message){
   try {string p=System.IO.Path.Combine(Root,"events.log");if(File.Exists(p)&&new FileInfo(p).Length>1024*1024){File.Copy(p,p+".1",true);File.Delete(p);}File.AppendAllText(p,DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss")+"  "+message+Environment.NewLine,Encoding.UTF8);}catch(IOException){}catch(UnauthorizedAccessException){}
  }
 }
 public static class Startup {
  const string Key=@"Software\Microsoft\Windows\CurrentVersion\Run";
  const string Name="LinglongGuard";
  public static bool Enabled {get {using(var k=Registry.CurrentUser.OpenSubKey(Key)){return k!=null&&string.Equals(k.GetValue(Name) as string,Command,StringComparison.OrdinalIgnoreCase);}}}
  public static string Command {get{return "\""+System.Windows.Forms.Application.ExecutablePath+"\" --startup";}}
  public static void Set(bool enabled){using(var k=Registry.CurrentUser.CreateSubKey(Key)){if(enabled){if(Command.Length>260)throw new InvalidOperationException("软件路径过长，请把程序放到更短的路径再开启自启。");k.SetValue(Name,Command,RegistryValueKind.String);}else k.DeleteValue(Name,false);}}
 }
 public static class Importer {
  public static AppEntry Resolve(string file) {
   string path=System.IO.Path.GetFullPath(file),args="",cwd="";
   if(!File.Exists(path))throw new FileNotFoundException("文件不存在",path);
   if(string.Equals(System.IO.Path.GetExtension(path),".lnk",StringComparison.OrdinalIgnoreCase)) {
    object shell=null,link=null;
    try {
     var type=Type.GetTypeFromProgID("WScript.Shell");shell=Activator.CreateInstance(type);
     link=type.InvokeMember("CreateShortcut",System.Reflection.BindingFlags.InvokeMethod,null,shell,new object[]{path});
     Func<string,string> get=n=>Convert.ToString(link.GetType().InvokeMember(n,System.Reflection.BindingFlags.GetProperty,null,link,null));
     path=get("TargetPath");args=get("Arguments");cwd=get("WorkingDirectory");
    } finally {if(link!=null)Marshal.FinalReleaseComObject(link);if(shell!=null)Marshal.FinalReleaseComObject(shell);}
   }
   path=Environment.ExpandEnvironmentVariables(path??"");cwd=Environment.ExpandEnvironmentVariables(cwd??"");
   ValidatePath(path);
   if(string.IsNullOrWhiteSpace(cwd))cwd=System.IO.Path.GetDirectoryName(path);
   if(!System.IO.Directory.Exists(cwd))throw new DirectoryNotFoundException("工作目录不存在："+cwd);
   return new AppEntry{Name=System.IO.Path.GetFileNameWithoutExtension(file),Path=System.IO.Path.GetFullPath(path),Arguments=args??"",Directory=cwd};
  }
  public static void ValidatePath(string path){
   if(string.IsNullOrWhiteSpace(path)||!string.Equals(System.IO.Path.GetExtension(path),".exe",StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("请拖入 .exe 程序或指向它的 .lnk 快捷方式；暂不支持商店应用、网页和脚本入口。");
   if(!File.Exists(path))throw new FileNotFoundException("快捷方式指向的程序不存在",path);
   string full=System.IO.Path.GetFullPath(path);
   if(string.Equals(full,System.Windows.Forms.Application.ExecutablePath,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("不能把守护软件自身加入守护。");
   string windows=Environment.GetFolderPath(Environment.SpecialFolder.Windows).TrimEnd('\\')+"\\";
   if(full.StartsWith(windows,StringComparison.OrdinalIgnoreCase))throw new InvalidOperationException("不管理 Windows 系统目录中的程序，请选择普通桌面软件。");
  }
 }
}
