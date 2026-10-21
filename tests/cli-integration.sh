#!/bin/sh
set -eu
app=/usr/bin/login-startup
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
export XDG_CONFIG_HOME="$tmp/config"
script="$tmp/启动 = 100% \$x.sh"
cat > "$script" <<'EOF'
#!/bin/sh
printf '%s\n' "$PWD" "$1" >> "$2"
EOF
args="'literal \$HOME ; %f' '$tmp/marker'"
id=$($app --add "$script" --args "$args")
test ! -e "$tmp/marker"
test "$($app --list | wc -l)" = 1
entry="$XDG_CONFIG_HOME/autostart/login-startup-$id.desktop"
desktop-file-validate "$entry"
$app --try "$id"
i=0
while [ ! -f "$tmp/marker" ] && [ "$i" -lt 50 ]; do sleep .1; i=$((i+1)); done
test "$(sed -n '1p' "$tmp/marker")" = "$tmp"
test "$(sed -n '2p' "$tmp/marker")" = 'literal $HOME ; %f'
$app --disable "$id"
$app --launch "$id"
test "$(wc -l < "$tmp/marker")" = 2
$app --try "$id"
i=0
while [ "$(wc -l < "$tmp/marker")" -lt 4 ] && [ "$i" -lt 50 ]; do sleep .1; i=$((i+1)); done
test "$(wc -l < "$tmp/marker")" = 4
$app --enable "$id"
$app --launch "$id"
test "$(wc -l < "$tmp/marker")" = 6
test "$($app --add "$script" --name Updated --args "$args")" = "$id"
test "$($app --list | wc -l)" = 1
$app --remove "$id"
test -f "$script"
test ! -e "$entry"
$app --inspect "$script" > /dev/null
test "$($app --list | wc -l)" = 0
cat > "$tmp/import.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Imported application
Exec=/usr/bin/touch $tmp/import-marker
Icon=utilities-terminal
Terminal=false
Path=/tmp
X-Test-Preserve=literal-value
EOF
desktop_id=$($app --add "$tmp/import.desktop")
test ! -e "$tmp/import-marker"
desktop_entry="$XDG_CONFIG_HOME/autostart/login-startup-$desktop_id.desktop"
grep -q 'X-Test-Preserve=literal-value' "$desktop_entry"
grep -q "Exec=/usr/bin/touch $tmp/import-marker" "$desktop_entry"
desktop-file-validate "$desktop_entry"
$app --try "$desktop_id"
i=0
while [ ! -f "$tmp/import-marker" ] && [ "$i" -lt 50 ]; do sleep .1; i=$((i+1)); done
test -e "$tmp/import-marker"
$app --remove "$desktop_id"
if $app --remove ../../etc/passwd 2>/dev/null; then exit 1; fi
echo 'PASS: installed CLI add/no-run/try/disabled-explicit-try/quoted-path/args/cwd/disable/enable/update/remove/import/inspect'
