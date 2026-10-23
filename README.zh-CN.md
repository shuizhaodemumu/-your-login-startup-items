# 你的登录启动项

一款使用 GTK4 编写的桌面软件，用来管理登录 GNOME 桌面后自动启动的文件。首个版本面向 Ubuntu 26.04 LTS amd64。

[English](README.md)

## 功能

- 将本机程序、脚本、文档和现有 `.desktop` 快捷方式加入当前用户的登录启动列表。
- 启用、禁用、编辑、删除启动项，并可单独试运行。
- 设置工作目录、启动参数、登录后延迟（0–3600 秒）和静默启动选项。
- 为单个启动项启用窗口置顶，后台助手会持续将其 X11 / XWayland 窗口保持在普通窗口上方。
- 导入 `.desktop` 快捷方式时保留其原命令、图标、终端选项和桌面限制。
- 扫描由已安装 deb 包提供的图形应用，可搜索、多选并批量创建桌面快捷方式。
- 对可识别进程的程序或应用快捷方式启用看门狗：用户登录时自动启动后台助手；目标进程退出后重新启动，并按 2、4、8……最高 60 秒的间隔退避重试。
- 添加启动项只读取配置并保存快捷方式，不会运行所选文件。
- 只管理本软件创建的启动项。删除启动配置不会删除原文件。

应用扫描只包含 `/usr/share/applications` 中由当前已安装 deb 包拥有、且具有可显示启动器的图形应用。库、命令行工具和没有桌面启动器的包无法自动生成可靠的桌面快捷方式。看门狗支持直接启动的可执行程序、命令可识别的 `.desktop` 应用，以及有执行权限的 `.run` 启动器；`.run` 启动器须在进程命令行或环境变量中保留来源路径，助手才能准确识别其进程。脚本和 `env`/Shell 包装命令仍不支持。手动关闭受监控程序也会触发重启；可随时关闭对应看门狗。

启动延迟使用 GNOME 自启动设置，单位为秒，0 表示不延迟。静默启动会关闭终端窗口和 GNOME 启动通知；它不会隐藏目标程序自己的窗口，也不能阻止目标程序主动弹出的对话框。

窗口置顶依赖 X11 的 EWMH `_NET_WM_STATE_ABOVE`。它适用于 X11 和通过 XWayland 运行的程序；Wayland 原生窗口受桌面隔离机制保护，其他程序无法通用地读取或强制置顶。全屏窗口、锁屏界面及桌面环境自身的策略仍可能覆盖置顶状态。

## 系统要求

- Ubuntu 26.04 LTS、GNOME 桌面、amd64
- 构建需要 C 编译器、Meson、Ninja、GTK4、Libadwaita、GLib、X11 开发头文件和 Debian 打包工具
- 终端试运行需要 `xdg-terminal-exec`

## 构建 Debian 安装包

```sh
sudo apt install build-essential meson ninja-build libgtk-4-dev libadwaita-1-dev libx11-dev desktop-file-utils dpkg-dev
sh packaging/build-deb.sh
sudo apt install ./dist/login-startup_0.2.4_amd64.deb
```

运行 `login-startup`，或从应用程序菜单打开“你的登录启动项”。安装包面向 Ubuntu 26.04 LTS，不承诺兼容更早版本。

## 命令行示例

```sh
login-startup --add /path/to/program
login-startup --list
login-startup --try ITEM_ID
login-startup --disable ITEM_ID
login-startup --enable ITEM_ID
login-startup --remove ITEM_ID
login-startup --inspect /path/to/file
login-startup --list-installed-apps
login-startup --watchdog-enable ITEM_ID
login-startup --watchdog-disable ITEM_ID
```

`--inspect` 只检查文件，不保存配置或运行文件。启动参数不会经过 shell 展开。

## 数据与安全

启动项保存在 `~/.config/autostart/`。普通文件通过本软件的辅助程序启动。启用看门狗或窗口置顶时，助手会在同一目录创建单独的用户级登录启动项并在后台运行；关闭所有相关选项后该项会自动移除。目标程序须能通过 `/proc` 中的可执行路径识别。看门狗重启间隔逐次增加，最长 60 秒，不设最大重试次数。卸载软件前请先移除由它管理的启动项。导入的 `.desktop` 快捷方式保留自身的启动命令。

新建的应用桌面快捷方式放在当前用户配置的桌面目录中，以可执行副本形式保存原始 `.desktop` 文件；删除这些快捷方式不会卸载应用。

管理启动项时不跟随符号链接。文件移动后不会自动追踪。脚本仍会由脚本解释器执行，本软件无法判断脚本是否可信。

## 开发

程序使用 C、GTK4、Libadwaita 和 GLib 编写。Meson 用于构建应用和核心测试程序；相关测试会使用隔离的用户配置目录。

本版本的验证范围和限制见 [VALIDATION.md](VALIDATION.md)。

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。

## 版本说明

请参阅 [0.2.4 版本说明](RELEASE_NOTES_0.2.4.zh-CN.md)。

