# 玲珑守护 1.0.0 · Windows x64

本仓库新增 Windows 版本「玲珑守护」，使用白发紫瞳、紫色龙角的小龙娘「玲珑」主题。

## 下载与运行

- **`LinglongGuard-1.0.0-windows-x64.zip`**：Windows 10/11 x64 程序包。
- `LinglongGuard-1.0.0-source.zip`：Windows 源码、素材、构建脚本和测试。
- `SHA256SUMS.txt`：下载文件的 SHA-256 校验值。

完整解压程序包，运行 `LinglongGuard.exe`，保留同目录 DLL。需要 .NET Framework 4.8，无需安装 Rust、Node 或 Python。

## 功能

- 拖入 `.exe` 或 `.lnk` 快捷方式加入管理，自动导入参数与工作目录。
- 当前用户登录自启、每个程序独立启用自启与看门狗。
- 程序退出后拉起；连续无响应达到 10 秒后强制结束并重启。
- 恢复启动参数、工作目录、主窗口位置/大小及最大化、最小化状态。
- 托盘后台守护、暂停全部、运行日志和高频重启保护。
- 退出守护或移除条目时，目标程序继续运行。

## 验证与限制

5 项 Rust 单元测试通过；Gemini 3.8 Flash High 复验的 28 项原生和 15 项 UI/托盘断言全部通过。连续卡死测试约 10.41 秒触发恢复。

未保存文档、标签页和会话需要目标程序自行恢复。本版未实测真实开机登录、跨权限拖放、多显示器热插拔及复杂第三方程序长期兼容性；启动器转交进程等入口请优先改为实际主程序 EXE。

[使用说明](https://github.com/shuizhaodemumu/-your-login-startup-items/blob/main/windows/README.md) · [测试范围](https://github.com/shuizhaodemumu/-your-login-startup-items/blob/main/windows/VALIDATION.md)

---

## English

Linglong Guard 1.0.0 is the Windows x64 edition: drag-and-drop EXE/LNK import, per-user login startup, process-exit recovery, a 10-second unresponsive-window watchdog, window placement restoration, and tray controls. Extract the full Windows ZIP and run `LinglongGuard.exe`. Requires Windows 10/11 x64 and .NET Framework 4.8.

Unsaved application data cannot be restored generically. See the Windows README and validation notes for supported entry points and untested scenarios. The Ubuntu edition retains its existing implementation and version series.
