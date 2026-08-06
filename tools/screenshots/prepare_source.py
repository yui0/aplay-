#!/usr/bin/env python3
"""Create a screenshot-only translation unit without changing aplay+ui.c.

The GUI implementation is copied byte-for-byte. Only the playback/CLI regions
are wrapped out so the renderer can be linked to a tiny deterministic engine
stub in environments that do not have ALSA or GLFW development headers.
"""
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit(f"usage: {sys.argv[0]} INPUT OUTPUT")

src = Path(sys.argv[1]).read_text(encoding="utf-8")

first = "static LS_LIST *gui_ls_dir(char *dir, int flag, int *num)"
second = "// ============================================================\n// luna-ui GUI"
third = "static void *audio_engine_thread(void *arg)"
for marker in (first, second, third):
    if marker not in src:
        raise SystemExit(f"prepare_source.py: source marker not found: {marker!r}")

src = src.replace(first, "#ifndef APLAY_SCREENSHOT_BUILD\n" + first, 1)
src = src.replace(second, "#endif /* !APLAY_SCREENSHOT_BUILD */\n\n" + second, 1)
src = src.replace(third, "#ifndef APLAY_SCREENSHOT_BUILD\n" + third, 1)
src += "\n#endif /* !APLAY_SCREENSHOT_BUILD */\n"
Path(sys.argv[2]).write_text(src, encoding="utf-8")
