#!/bin/sh
# End-to-end paste check on a real X11 display (CI runs it under xvfb-run):
# puts HTML-only data (an <ol> with <code>, no plain-text target) on the
# CLIPBOARD with xclip, starts the real `ze` on an empty file, types
# i + Ctrl-V + Escape + Ctrl-S with xdotool, and checks what got saved.
#
#   tests/e2e/x11_paste_e2e.sh path/to/ze [screenshot.png]
#
# Needs $DISPLAY, xclip, xdotool (and ImageMagick `import` for the optional
# screenshot). Exits non-zero if the saved buffer isn't the plain text.
set -eu
ze=$1
shot=${2:-}
work=$(mktemp -d)
trap 'kill "$ze_pid" 2>/dev/null || true; rm -rf "$work"' EXIT
out=$work/pasted.txt

printf '%s' "<meta charset='utf-8'><p>To fix the build:</p><ol><li>Run <code>cmake -B build</code></li><li>Then run <code>ctest --test-dir build</code></li></ol>" > "$work/copy.html"
printf 'To fix the build:\n\n1. Run cmake -B build\n2. Then run ctest --test-dir build' > "$work/expected.txt"
xclip -selection clipboard -t text/html -i "$work/copy.html"
echo "CLIPBOARD TARGETS: $(xclip -selection clipboard -o -t TARGETS | tr '\n' ' ')"

env -u WAYLAND_DISPLAY "$ze" "$out" > "$work/ze.log" 2>&1 &
ze_pid=$!
win=
for _ in $(seq 100); do
  win=$(xdotool search --name '^zedit' 2>/dev/null | head -1) || true
  [ -n "$win" ] && break
  sleep 0.2
done
[ -n "$win" ] || { echo "ze window never appeared"; cat "$work/ze.log"; exit 1; }
sleep 2
xdotool windowfocus "$win"
sleep 0.5
xdotool type --delay 80 "i"
xdotool key ctrl+v
sleep 0.5
xdotool key Escape
sleep 0.3
xdotool key ctrl+s
for _ in $(seq 50); do [ -s "$out" ] && break; sleep 0.1; done
[ -n "$shot" ] && import -window root "$shot" 2>/dev/null || true

echo "--- ze stderr:"; cat "$work/ze.log"
echo "--- saved buffer:"; cat "$out" 2>/dev/null || echo "(nothing saved)"; echo
if cmp -s "$out" "$work/expected.txt"; then
  echo "PASS: HTML-only clipboard pasted into ze as plain text"
else
  echo "FAIL: expected:"; cat "$work/expected.txt"; echo
  exit 1
fi
