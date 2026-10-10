#!/usr/bin/env bash
# film.sh [OUT.mp4] - CTermTour filmed off FS-UAE at full frame rate.
# FS-UAE runs in cage, a Wayland compositor with no screen (headless,
# nothing on the desktop), and wf-recorder films cage's output. The
# machine: an A1200 with 8 MB of fast RAM (68020 14 MHz, AGA, the
# 16-colour Workbench). Keys come over wasabi, as a person would press
# them. Needs: cage, wf-recorder, fs-uae, ffmpeg.
set -u
out=${1:-$HOME/Projects/CTerm/demo/CTerm-tour-uae.mp4}
H="$HOME/FS-UAE/Hard Drives/AmigaOS3.2"; export WASABI_HOST=127.0.0.1
D=$(cd "$(dirname "$0")/../.." && pwd)
export XDG_RUNTIME_DIR=/run/user/$(id -u)
if pgrep -x fs-uae >/dev/null; then
  echo "film.sh: an emulator is already running (another session?) - not starting" >&2
  exit 1
fi
cp "$D/CTerm" "$H/T/CTerm"; cp "$D/CTermTour" "$H/T/CTermTour"
printf 'SYS:T/CTermTour SECS 0 LINES 500 RACEWIN "CON:0/12/640/244/CON: the Amiga'"'"'s own console/CLOSE"\nEndCLI\n' > "$H/S/xtour"
before=$(ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | sort)
(cd ~/FS-UAE && WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1 WLR_RENDERER=pixman \
   WLR_HEADLESS_OUTPUTS=1 SDL_VIDEODRIVER=wayland setsid cage -- \
   fs-uae "$D/tests/stock/A1200-Stock-fast.fs-uae" --zoom=640x512 --keep_aspect=1 \
   --scanlines=0 --fullscreen=1 >/tmp/film-cage.log 2>&1 &)
sleep 3
# cage's display: the socket that was not there before
wd=$(comm -13 <(echo "$before") <(ls "$XDG_RUNTIME_DIR" | grep -E '^wayland-[0-9]+$' | sort) | head -1)
if [ -z "$wd" ]; then echo "film.sh: cage did not start" >&2; exit 1; fi
until wasabi ping >/dev/null 2>&1; do sleep 2; done
sleep 4
wasabi run --max-time 5 "Run >NIL: SYS:T/CTerm COLS 78 ROWS 29 FROM S:xtour" >/dev/null
wasabi wait window CTerm --timeout 60 >/dev/null
sleep 2
WAYLAND_DISPLAY=$wd wf-recorder -r 50 -c libx264 -p preset=veryfast -p crf=18 -f /tmp/film-raw.mp4 \
   >/tmp/film-wfr.log 2>&1 &
rec=$!
sleep 3
for s in 6 6 6 6 6 6 15 6 6 7; do       # pages 1..10
  sleep "$s"; wasabi key press space >/dev/null
done
sleep 4; wasabi key type r >/dev/null    # the race: CON: first, then CTerm
for i in $(seq 1 120); do                # CON:'s window closes when it is done
  sleep 1
  wasabi windows 2>/dev/null | grep -q "own console" || break
done
sleep 1; wasabi mouse click 320 150 >/dev/null   # CTerm has the keys again
sleep 6; wasabi key press space >/dev/null       # the last page
sleep 5
kill -INT "$rec"; wait "$rec" 2>/dev/null
wasabi key press esc >/dev/null; sleep 1
pid=$(pgrep -n -x fs-uae); kill "$pid" 2>/dev/null; pkill -x cage 2>/dev/null
while kill -0 "$pid" 2>/dev/null; do sleep 1; done
mkdir -p "$(dirname "$out")"
ffmpeg -v error -y -i /tmp/film-raw.mp4 -c:v libx264 -preset slow -crf 20 \
   -pix_fmt yuv420p -movflags +faststart "$out" && echo "$out"
