#!/bin/sh
# A harmless fixture: append one line when this desktop session launches it.
set -eu
dir="$HOME/login-startup/work"
mkdir -p "$dir"
printf '%s\t%s\t%s\n' "$(date -Iseconds)" "${XDG_SESSION_ID:-unknown}" "$(cat /proc/sys/kernel/random/boot_id)" >> "$dir/login-marker.tsv"
