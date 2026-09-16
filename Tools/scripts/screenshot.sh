#!/usr/bin/env bash
#
#  Launches the NACAR standalone under a virtual X server and captures the
#  interface to a PNG, so UI work can be checked against the locked reference
#  image instead of assumed.
#
#  Usage:  Tools/scripts/screenshot.sh <output.png> [seconds-to-settle] [W] [H]
#
set -euo pipefail

OUT="${1:-nacar.png}"
SETTLE="${2:-6}"
W="${3:-1536}"
H="${4:-1024}"

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APP="$ROOT/build/NACAR_artefacts/Release/Standalone/NACAR"
[ -x "$APP" ] || APP="$ROOT/build/NACAR_artefacts/Standalone/NACAR"

if [ ! -x "$APP" ]; then
    echo "standalone not built: $APP" >&2
    exit 1
fi

DISPLAY_NUM=":99"
# A little headroom around the window so the capture is not clipped by the
# window manager's own decorations.
Xvfb "$DISPLAY_NUM" -screen 0 "$((W + 120))x$((H + 160))x24" -nolisten tcp &
XVFB_PID=$!
trap 'kill $XVFB_PID 2>/dev/null || true' EXIT

sleep 2

# The standalone wants to open an audio device; the dummy ALSA path is enough
# for it to reach the editor, and we do not care about the audio here.
DISPLAY="$DISPLAY_NUM" "$APP" >/dev/null 2>&1 &
APP_PID=$!

sleep "$SETTLE"

DISPLAY="$DISPLAY_NUM" import -window root "$OUT" 2>/dev/null \
    || DISPLAY="$DISPLAY_NUM" xwd -root -silent | convert xwd:- "$OUT"

kill "$APP_PID" 2>/dev/null || true
wait "$APP_PID" 2>/dev/null || true

echo "wrote $OUT ($(identify -format '%wx%h' "$OUT" 2>/dev/null || echo '?'))"
