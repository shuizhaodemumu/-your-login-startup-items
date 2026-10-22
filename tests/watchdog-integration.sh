#!/bin/sh
set -eu
app=${1:?expected login-startup binary}
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
export XDG_CONFIG_HOME="$tmp/config"
export XDG_CACHE_HOME="$tmp/cache"
export XDG_DATA_HOME="$tmp/data"
export PATH="$(dirname "$app"):$PATH"
marker="$tmp/restarts"
source="$tmp/watched.c"
program="$tmp/watched"
cat > "$source" <<EOF
#include <stdio.h>
int main(void) {
    FILE *file = fopen("$marker", "a");
    if (!file) return 1;
    fputs("restart\\n", file);
    fclose(file);
    return 0;
}
EOF
cc "$source" -o "$program"
id=$("$app" --add "$program")
"$app" --watchdog-enable "$id"

i=0
while [ "$(wc -l < "$marker" 2>/dev/null || echo 0)" -lt 2 ] && [ "$i" -lt 35 ]; do
    sleep 1
    i=$((i + 1))
done
test "$(wc -l < "$marker")" -ge 2

"$app" --watchdog-disable "$id"
before=$(wc -l < "$marker")
sleep 4
after=$(wc -l < "$marker")
test "$after" = "$before"
"$app" --remove "$id"

desktop="$tmp/watched.desktop"
cat > "$desktop" <<EOF
[Desktop Entry]
Type=Application
Name=Watched desktop app
Exec=$program
Terminal=false
EOF
desktop_id=$("$app" --add "$desktop")
"$app" --watchdog-enable "$desktop_id"
i=0
while [ "$(wc -l < "$marker")" -lt $((before + 2)) ] && [ "$i" -lt 35 ]; do
    sleep 1
    i=$((i + 1))
done
test "$(wc -l < "$marker")" -ge $((before + 2))
"$app" --watchdog-disable "$desktop_id"
before=$(wc -l < "$marker")
sleep 4
test "$(wc -l < "$marker")" = "$before"
"$app" --remove "$desktop_id"
echo 'PASS: watchdog auto-started a missing process again and stopped relaunching after disable.'
