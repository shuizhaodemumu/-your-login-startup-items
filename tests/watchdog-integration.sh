#!/bin/sh
set -eu
app=${1:?expected login-startup binary}
tmp=$(mktemp -d)
run_pid="$tmp/watched-run.pid"
cleanup() {
    if [ -f "$run_pid" ]; then kill "$(cat "$run_pid")" 2>/dev/null || :; fi
    rm -rf "$tmp"
}
trap cleanup EXIT INT TERM
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

run_source="$tmp/watched.run"
run_marker="$tmp/run-restarts"
run_stable_copy="$tmp/stable-copy.run"
cat > "$run_source" <<EOF
#!/bin/sh
export APP_RUN_BUNDLE="$run_stable_copy"
echo "restart" >> "$run_marker"
printf '%s\\n' "\$\$" > "$run_pid"
exec sleep 120
EOF
cat >> "$run_source" <<EOF
# tail -c +1001 -- "\$self" | head -c 4 > /dev/null
EOF
python3 - "$run_source" <<'PY'
import pathlib, sys
path = pathlib.Path(sys.argv[1])
data = path.read_bytes()
if len(data) >= 1000:
    raise SystemExit(".run test header unexpectedly exceeds 1000 bytes")
data += b"#" * (1000 - len(data))
path.write_bytes(data + b"\x00\xffAB")
PY
chmod 700 "$run_source"
cp -- "$run_source" "$run_stable_copy"
printf 'extra trailing data' >> "$run_stable_copy"
run_id=$("$app" --add "$run_source")
"$run_source" &
i=0
while [ "$(wc -l < "$run_marker" 2>/dev/null || echo 0)" -lt 1 ] && [ "$i" -lt 10 ]; do
    sleep 1
    i=$((i + 1))
done
test "$(wc -l < "$run_marker")" -eq 1
"$app" --watchdog-enable "$run_id"
i=0
while [ "$i" -lt 4 ]; do
    sleep 1
    i=$((i + 1))
done
test "$(wc -l < "$run_marker")" -eq 1
kill "$(cat "$run_pid")"
i=0
while [ "$(wc -l < "$run_marker" 2>/dev/null || echo 0)" -lt 2 ] && [ "$i" -lt 20 ]; do
    sleep 1
    i=$((i + 1))
done
test "$(wc -l < "$run_marker")" -ge 2
"$app" --watchdog-disable "$run_id"
before=$(wc -l < "$run_marker")
kill "$(cat "$run_pid")"
sleep 4
test "$(wc -l < "$run_marker")" = "$before"
"$app" --remove "$run_id"
echo 'PASS: watchdog auto-started a missing process again and stopped relaunching after disable.'
