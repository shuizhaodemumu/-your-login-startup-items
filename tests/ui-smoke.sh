#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
set +e
GTK_A11Y=none GDK_DISABLE=gl,vulkan GSK_RENDERER=cairo G_DEBUG=fatal-warnings XDG_CONFIG_HOME="$tmp/config" timeout 4s build/login-startup > "$tmp/stdout" 2> "$tmp/stderr"
status=$?
set -e
cat "$tmp/stderr"
if [ "$status" != 124 ]; then
    echo "FAIL: GUI exited unexpectedly ($status)"
    exit 1
fi
if [ -s "$tmp/stderr" ]; then
    echo 'FAIL: GUI produced diagnostics'
    exit 1
fi
echo 'PASS: GTK4/Libadwaita window remained alive with fatal warnings enabled'
