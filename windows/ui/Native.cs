using System;
using System.Runtime.InteropServices;

namespace DragonGuard {
 [StructLayout(LayoutKind.Sequential)] public struct Point { public int X,Y; }
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct Placement { public uint Length,Flags,Show; public Point Min,Max; public Rect Rect; }
 [StructLayout(LayoutKind.Sequential)] public struct Snapshot { public uint State,Pid,Restarts,HungMs,Error,Reason,HasPlacement; public Placement Placement; }
 public static class Native {
  const string Lib="dragon_guard_core.dll";
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] public static extern IntPtr dg_create(string path,string args,string cwd);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] public static extern void dg_free(IntPtr guard);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] public static extern uint dg_start(IntPtr guard);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] public static extern uint dg_restart(IntPtr guard);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] public static extern void dg_tick(IntPtr guard,out Snapshot snapshot);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] public static extern void dg_snapshot(IntPtr guard,out Snapshot snapshot);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] public static extern void dg_enable(IntPtr guard,int enabled);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl,CharSet=CharSet.Unicode)] public static extern void dg_configure(IntPtr guard,string args,string cwd);
  [DllImport(Lib,CallingConvention=CallingConvention.Cdecl)] public static extern void dg_restore(IntPtr guard,ref Placement placement);
  public static string ErrorText(uint code) {
   switch(code){
    case 100001:return "发现多个独立实例，请只保留一个后重试";
    case 100002:return "60 秒内已重启 5 次，已暂停自动重启";
    case 100003:return "旧进程未能完全退出，已暂停重启";
    case 100004:return "接管时进程已变化，请点击启动重试";
    case 5:return "权限不足，请让守护软件与目标程序使用相同权限";
    case 740:return "目标需要管理员权限，请手动以管理员身份运行守护软件";
    case 2:case 3:return "程序或工作目录不存在，请检查路径";
    default:return new System.ComponentModel.Win32Exception((int)code).Message+" ("+code+")";
   }
  }
 }
}
