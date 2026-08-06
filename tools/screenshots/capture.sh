#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT="$ROOT/screenshots"

python3 "$HERE/generate_sapphire_skin.py"
"$HERE/build.sh"
mkdir -p "$OUT"
cd "$HERE"

export TERM=${TERM:-xterm}
export LIBGL_ALWAYS_SOFTWARE=${LIBGL_ALWAYS_SOFTWARE:-1}

"$HERE/.build/capture" --output "$OUT/aplay-ember.png" --variant 0
"$HERE/.build/capture" --output "$OUT/aplay-sapphire-skin.png" \
    --skin "$HERE/skin/sapphire" --variant 1
"$HERE/.build/capture" --output "$OUT/aplay-context-menu.png" --variant 2 --menu
