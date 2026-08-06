#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
BUILD="$HERE/.build"
CC=${CC:-cc}

mkdir -p "$BUILD"
python3 "$HERE/prepare_source.py" "$ROOT/aplay+ui.c" "$BUILD/aplay+ui.c"
cp "$HERE/engine_stub.h" "$BUILD/aplay+engine.h"

# Link against the runtime sonames so libEGL/libGL development symlinks are
# not required. OpenGL entry points themselves are resolved through EGL.
"$CC" -std=c11 -O2 -w \
    -I"$HERE/include" -I"$BUILD" -I"$ROOT" \
    -o "$BUILD/capture" \
    "$HERE/capture.c" "$HERE/glfw_compat.c" \
    -Wl,-l:libEGL.so.1 -ldl -lm -lpthread
