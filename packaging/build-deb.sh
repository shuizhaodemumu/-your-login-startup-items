#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
meson setup build --prefix=/usr --buildtype=release --wipe
meson compile -C build
meson test -C build --print-errorlogs
mkdir -p dist
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT INT TERM
DESTDIR="$stage" meson install -C build
strip "$stage/usr/bin/login-startup"
mkdir -p "$stage/DEBIAN"
deps=$(dpkg-shlibdeps -O -e build/login-startup 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
test -n "$deps"
cat > "$stage/DEBIAN/control" <<EOF
Package: login-startup
Version: 0.2.4
Section: utils
Priority: optional
Architecture: $(dpkg --print-architecture)
Installed-Size: $(du -sk "$stage/usr" | cut -f1)
Maintainer: Local Desktop Tools <local@example.invalid>
Depends: $deps, xdg-utils, xdg-terminal-exec
Description: Native GTK4 login startup manager
 Manage per-user desktop autostart entries for programs, scripts,
 documents and application shortcuts. Never runs a file when adding it.
EOF
desktop-file-validate "$stage/usr/share/applications/io.github.local.LoginStartup.desktop"
find "$stage" -type d -exec chmod 755 {} +
package_file="dist/login-startup_0.2.4_$(dpkg --print-architecture).deb"
dpkg-deb --root-owner-group --build "$stage" "$package_file"
dpkg-deb --info "$package_file"
dpkg-deb --contents "$package_file"
