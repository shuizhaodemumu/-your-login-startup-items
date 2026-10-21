# 你的登录启动项

一款使用 GTK4 编写的桌面软件，用来管理登录 GNOME 桌面后自动启动的文件。首个版本面向 Ubuntu 26.04 LTS amd64。

[English](README.md)

## 功能

- 将本机程序、脚本、文档和现有 `.desktop` 快捷方式加入当前用户的登录启动列表。
- 启用、禁用、编辑、删除启动项，并可单独试运行。
- 设置工作目录、启动参数和终端选项。
- 导入 `.desktop` 快捷方式时保留其原命令、图标、终端选项和桌面限制。
- 添加启动项只读取配置并保存快捷方式，不会运行所选文件。
- 只管理本软件创建的启动项。删除启动配置不会删除原文件。

## 系统要求

- Ubuntu 26.04 LTS、GNOME 桌面、amd64
- 构建需要 C 编译器、Meson、Ninja、GTK4、Libadwaita、GLib 和 Debian 打包工具
- 终端试运行需要 `xdg-terminal-exec`

## 构建 Debian 安装包

```sh
sudo apt install build-essential meson ninja-build libgtk-4-dev libadwaita-1-dev desktop-file-utils dpkg-dev
sh packaging/build-deb.sh
sudo apt install ./dist/login-startup_0.1.0_amd64.deb
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
```

`--inspect` 只检查文件，不保存配置或运行文件。启动参数不会经过 shell 展开。

## 数据与安全

启动项保存在 `~/.config/autostart/`。普通文件通过本软件的辅助程序启动。卸载软件前请先移除由它管理的启动项。导入的 `.desktop` 快捷方式保留自身的启动命令。

管理启动项时不跟随符号链接。文件移动后不会自动追踪。脚本仍会由脚本解释器执行，本软件无法判断脚本是否可信。

## 开发

程序使用 C、GTK4、Libadwaita 和 GLib 编写。Meson 用于构建应用和核心测试程序；相关测试会使用隔离的用户配置目录。

首个版本的验证范围和限制见 [VALIDATION.md](VALIDATION.md)。

## 许可证

本项目采用 MIT 许可证，详见 [LICENSE](LICENSE)。
