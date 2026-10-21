#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
deb=dist/login-startup_0.1.0_amd64.deb
test "$(dpkg-deb -f "$deb" Package)" = login-startup
test "$(dpkg-deb -f "$deb" Architecture)" = amd64
dpkg-deb -f "$deb" Version Depends
desktop-file-validate /usr/share/applications/io.github.local.LoginStartup.desktop
test -x /usr/bin/login-startup
/usr/bin/login-startup --version
if ldd /usr/bin/login-startup | grep 'not found'; then exit 1; fi
test "$(dpkg-query -W -f='${Status}' login-startup)" = 'install ok installed'
echo 'PASS: package architecture, dependencies, desktop entry, ELF libraries and installed state'
