#!/usr/bin/env bash
set -euo pipefail

export DEBIAN_FRONTEND=noninteractive
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  build-essential libasound2-dev libglfw3-dev libgl1-mesa-dev \
  xvfb openbox x11-xserver-utils xdotool wmctrl scrot \
  python3 python3-pil fonts-dejavu-core unzip

make clean
make ui

WORK="${RUNNER_TEMP}/aplay-readme-capture"
DEMO="${WORK}/music"
SKIN="${WORK}/skin"
RAW="${WORK}/raw"
rm -rf "${WORK}"
mkdir -p "${DEMO}" "${SKIN}" "${RAW}"

python3 - "${DEMO}" <<'PY'
import math
import os
import struct
import sys
import wave

out = sys.argv[1]
tracks = [
    ("01 - Aurora Skyline.wav", 220.0, 277.18),
    ("02 - Copper Moon.wav", 196.0, 246.94),
    ("03 - Midnight Drive.wav", 164.81, 220.0),
]
rate = 44100
seconds = 45
for name, left_hz, right_hz in tracks:
    path = os.path.join(out, name)
    with wave.open(path, "wb") as wav:
        wav.setnchannels(2)
        wav.setsampwidth(2)
        wav.setframerate(rate)
        chunk = bytearray()
        for i in range(rate * seconds):
            fade = min(1.0, i / (rate * 0.08), (rate * seconds - i) / (rate * 0.15))
            l = int(6200 * fade * math.sin(2 * math.pi * left_hz * i / rate))
            r = int(6200 * fade * math.sin(2 * math.pi * right_hz * i / rate))
            chunk += struct.pack("<hh", l, r)
            if len(chunk) >= 262144:
                wav.writeframesraw(chunk)
                chunk.clear()
        if chunk:
            wav.writeframesraw(chunk)
PY

python3 - "${SKIN}" <<'PY'
from PIL import Image, ImageDraw, ImageFont
import os
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)

# An original Winamp Classic-compatible skin made solely for this screenshot.
main = Image.new("RGB", (275, 116), "#11162a")
p = main.load()
for y in range(main.height):
    for x in range(main.width):
        glow = max(0, 1 - ((x - 205) ** 2 + (y - 28) ** 2) ** 0.5 / 180)
        p[x, y] = (
            int(13 + 18 * glow + y * 0.04),
            int(17 + 20 * glow + y * 0.03),
            int(35 + 50 * glow + y * 0.08),
        )
d = ImageDraw.Draw(main)
d.rectangle((0, 0, 274, 13), fill="#20284a")
d.line((0, 14, 274, 14), fill="#52e6d8", width=1)
d.rounded_rectangle((9, 21, 99, 63), radius=4, fill="#08111f", outline="#314068")
d.text((18, 29), "APLAY+", fill="#8ffbf0")
d.text((18, 44), "BIT PERFECT", fill="#7d8db8")
d.rounded_rectangle((108, 21, 265, 63), radius=4, fill="#090d1b", outline="#314068")
d.text((116, 28), "MIDNIGHT NEON", fill="#eeeaff")
d.text((116, 44), "44.1 kHz  •  STEREO", fill="#ae9cff")
d.rectangle((10, 69, 265, 72), fill="#26304e")
d.rectangle((10, 69, 185, 72), fill="#e35cda")
d.ellipse((180, 66, 188, 75), fill="#f9d66f")
d.rounded_rectangle((180, 79, 265, 108), radius=4, fill="#121a31", outline="#33416d")
d.text((190, 88), "NEON 01", fill="#8ffbf0")
main.save(os.path.join(out, "MAIN.BMP"))

buttons = Image.new("RGB", (136, 36), "#11162a")
d = ImageDraw.Draw(buttons)
icons = ["|<", ">", "||", "[]", ">|", "^" ]
for state in range(2):
    y0 = state * 18
    for i, icon in enumerate(icons):
        x0 = i * 22
        fill = "#26365a" if state == 0 else "#3f2a63"
        edge = "#52e6d8" if state == 0 else "#e35cda"
        d.rounded_rectangle((x0 + 1, y0 + 1, x0 + 20, y0 + 16), radius=3, fill=fill, outline=edge)
        d.text((x0 + 5, y0 + 4), icon, fill="#f4f0ff")
buttons.save(os.path.join(out, "CBUTTONS.BMP"))

# Optional sheets keep the rest of the classic shell visually coherent.
title = Image.new("RGB", (275, 14), "#20284a")nd = ImageDraw.Draw(title)
nd.line((0, 13, 274, 13), fill="#52e6d8")
nd.text((7, 2), "aplay+  •  MIDNIGHT NEON", fill="#eeeaff")
title.save(os.path.join(out, "TITLEBAR.BMP"))
PY

# A null PCM keeps the player active in a headless runner without changing the app.
cat > "${WORK}/asound.conf" <<'EOF'
pcm.aplaynull {
  type null
}
EOF
export ALSA_CONFIG_PATH="${WORK}/asound.conf"
export DISPLAY=:99
export LIBGL_ALWAYS_SOFTWARE=1

Xvfb :99 -screen 0 1440x900x24 -nolisten tcp >"${WORK}/xvfb.log" 2>&1 &
XVFB_PID=$!
trap 'kill ${APP_PID:-0} ${OPENBOX_PID:-0} ${XVFB_PID:-0} 2>/dev/null || true' EXIT
sleep 1
openbox >"${WORK}/openbox.log" 2>&1 &
OPENBOX_PID=$!
xsetroot -solid '#11131c'

wait_for_app() {
  local tries=0
  while (( tries < 80 )); do
    if wmctrl -l | grep -qi 'aplay'; then
      sleep 2
      return 0
    fi
    if ! kill -0 "${APP_PID}" 2>/dev/null; then
      cat "${WORK}/app.log" >&2 || true
      return 1
    fi
    sleep 0.25
    tries=$((tries + 1))
  done
  wmctrl -l >&2 || true
  cat "${WORK}/app.log" >&2 || true
  return 1
}

stop_app() {
  if [[ -n "${APP_PID:-}" ]] && kill -0 "${APP_PID}" 2>/dev/null; then
    kill "${APP_PID}" 2>/dev/null || true
    for _ in $(seq 1 20); do
      kill -0 "${APP_PID}" 2>/dev/null || break
      sleep 0.15
    done
    kill -9 "${APP_PID}" 2>/dev/null || true
  fi
  APP_PID=""
  sleep 0.6
}

capture_root() {
  local name=$1
  scrot -o "${RAW}/${name}.png"
}

launch_default() {
  ./aplay+ui -l -d aplaynull "${DEMO}" >"${WORK}/app.log" 2>&1 &
  APP_PID=$!
  wait_for_app
}

launch_skin() {
  ./aplay+ui -l -d aplaynull --skin "${SKIN}" "${DEMO}" >"${WORK}/app-skin.log" 2>&1 &
  APP_PID=$!
  wait_for_app
}

launch_default
capture_root default

# The context menu is available from every aplay+ surface. Right-click the
# first visible app window at a safe interior point.
APP_WIN=$(wmctrl -l | awk 'tolower($0) ~ /aplay/ {print $1; exit}')
if [[ -n "${APP_WIN}" ]]; then
  eval "$(xdotool getwindowgeometry --shell "${APP_WIN}")"
  xdotool mousemove --sync $((X + WIDTH / 2)) $((Y + HEIGHT / 2)) click 3
  sleep 1
fi
capture_root menu
stop_app

launch_skin
capture_root skin
stop_app

rm -rf screenshots
mkdir -p screenshots

python3 - "${RAW}" screenshots <<'PY'
from PIL import Image, ImageDraw, ImageFilter, ImageFont
import os
import sys

raw_dir, out_dir = sys.argv[1:]
os.makedirs(out_dir, exist_ok=True)

specs = [
    ("default.png", "aplay-ui-overview.png", "EMBER EDITION", "BitPerfect playback, equalizer, and playlist"),
    ("menu.png", "aplay-ui-menu.png", "EVERY CONTROL, ONE CLICK AWAY", "Playback, DSP, devices, skins, and display options"),
    ("skin.png", "aplay-ui-skin.png", "WINAMP CLASSIC SKIN SUPPORT", "An original Midnight Neon skin running in aplay+"),
]

try:
    title_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 34)
    sub_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 18)
    badge_font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 14)
except OSError:
    title_font = sub_font = badge_font = ImageFont.load_default()

for src_name, dst_name, title, subtitle in specs:
    src = Image.open(os.path.join(raw_dir, src_name)).convert("RGB")
    bg = src.getpixel((0, 0))
    pix = src.load()
    xs, ys = [], []
    for y in range(src.height):
        for x in range(src.width):
            r, g, b = pix[x, y]
            if abs(r-bg[0]) + abs(g-bg[1]) + abs(b-bg[2]) > 24:
                xs.append(x); ys.append(y)
    if xs:
        box = (max(0, min(xs)-28), max(0, min(ys)-28), min(src.width, max(xs)+29), min(src.height, max(ys)+29))
        shot = src.crop(box)
    else:
        shot = src

    max_w, max_h = 1320, 720
    scale = min(max_w / shot.width, max_h / shot.height)
    if scale > 1.75:
        scale = 1.75
    if scale != 1:
        shot = shot.resize((int(shot.width * scale), int(shot.height * scale)), Image.Resampling.LANCZOS)

    W, H = 1600, 1000
    canvas = Image.new("RGB", (W, H))
    cp = canvas.load()
    for y in range(H):
        for x in range(W):
            t = y / H
            glow1 = max(0.0, 1.0 - ((x-260)**2 + (y-160)**2)**0.5 / 720)
            glow2 = max(0.0, 1.0 - ((x-1390)**2 + (y-760)**2)**0.5 / 800)
            cp[x, y] = (
                int(12 + 10*glow1 + 14*glow2 + 6*t),
                int(14 + 5*glow1 + 5*glow2 + 5*t),
                int(24 + 13*glow1 + 22*glow2 + 12*t),
            )

    x = (W - shot.width) // 2
    y = 194 + (720 - shot.height) // 2
    shadow = Image.new("RGBA", (W, H), (0,0,0,0))
    sd = ImageDraw.Draw(shadow)
    sd.rounded_rectangle((x-22, y-22, x+shot.width+22, y+shot.height+22), radius=34, fill=(0,0,0,180))
    shadow = shadow.filter(ImageFilter.GaussianBlur(24))
    canvas = Image.alpha_composite(canvas.convert("RGBA"), shadow)

    card = Image.new("RGBA", (shot.width+24, shot.height+24), (0,0,0,0))
    mask = Image.new("L", card.size, 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle((0,0,card.width-1,card.height-1), radius=24, fill=255)
    framed = Image.new("RGBA", card.size, (27,29,45,255))
    framed.paste(shot.convert("RGBA"), (12,12))
    canvas.paste(framed, (x-12,y-12), mask)

    draw = ImageDraw.Draw(canvas)
    draw.rounded_rectangle((88, 68, 222, 101), radius=16, fill=(233,111,67,255))
    draw.text((108, 77), "APLAY+", font=badge_font, fill=(255,255,255,255))
    draw.text((88, 118), title, font=title_font, fill=(246,242,238,255))
    draw.text((90, 162), subtitle, font=sub_font, fill=(172,178,199,255))
    draw.text((1390, 78), "LINUX  •  ALSA", font=badge_font, fill=(143,251,240,255), anchor="ra")

    canvas.convert("RGB").save(os.path.join(out_dir, dst_name), quality=95, optimize=True)
PY

python3 <<'PY'
from pathlib import Path
import re

path = Path("README.md")
text = path.read_text(encoding="utf-8")

start = text.index('<p align="center">\n  <img src="screenshots/aplay-ui-hero.png"')
end = text.index("## 💿 Supported File Formats", start)
intro = '''<p align="center">
  <img src="screenshots/aplay-ui-overview.png" alt="aplay+ Ember Edition — player, equalizer, and playlist" width="860">
</p>

<p align="center"><em>Ember Edition — Winamp-classic soul, audiophile controls, ALSA BitPerfect output</em></p>

<table>
  <tr>
    <td width="50%"><img src="screenshots/aplay-ui-menu.png" alt="aplay+ context menu with playback, DSP, device, and skin controls"></td>
    <td width="50%"><img src="screenshots/aplay-ui-skin.png" alt="aplay+ running an original Midnight Neon Winamp Classic skin"></td>
  </tr>
  <tr>
    <td align="center"><strong>Fast controls</strong><br><sub>Playback, DSP, devices, skins, and display options</sub></td>
    <td align="center"><strong>Classic skin support</strong><br><sub>Load a <code>.wsz</code> file or an extracted skin directory</sub></td>
  </tr>
</table>

'''
text = text[:start] + intro + text[end:]

old_menu = '''<p align="center">
  <img src="screenshots/aplay-ui-menu.png" alt="aplay+ Ember UI with right-click menu" width="640">
</p>'''
new_skin = '''<p align="center">
  <img src="screenshots/aplay-ui-skin.png" alt="aplay+ with an original Midnight Neon Winamp Classic skin" width="860">
</p>'''
text = text.replace(old_menu, new_skin)

text = re.sub(
    r'\n<p align="center">\n  <img src="screenshots/aplay-ui-alsa-flyout\.png"[^\n]*\n</p>\n',
    '\n',
    text,
)

path.write_text(text, encoding="utf-8")
PY

# Leave only the finished README assets in the branch; the one-shot capture
# workflow and helper script remove themselves in the same commit.
rm -f .github/workflows/capture-readme-screenshots.yml
rm -f tools/capture-readme-screenshots.sh
rmdir tools 2>/dev/null || true

git config user.name "github-actions[bot]"
git config user.email "41898282+github-actions[bot]@users.noreply.github.com"
git add -A
if ! git diff --cached --quiet; then
  git commit -m "Refresh README screenshots"
  git push origin "HEAD:${GITHUB_REF_NAME}"
fi
