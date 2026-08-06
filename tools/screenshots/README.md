# README screenshot renderer

This directory renders the real `aplay+ui` Luna/OpenGL interface for README
screenshots on a headless Linux worker that has no GLFW or ALSA development
packages.

`glfw_compat.c` is a deliberately small screenshot-only GLFW compatibility
layer. It creates shared surfaceless EGL pbuffer contexts, implements only the
GLFW calls used by `aplay+ui`, captures each real application surface with
`glReadPixels`, and composites the player, equalizer, playlist, and popup
windows into PNG files.

The source application is not replaced by a mock UI. `prepare_source.py`
copies `aplay+ui.c` to `.build/` and excludes only the CLI/playback-thread
regions for the capture executable. The complete GUI, HTML/CSS, skin loader,
Luna layout, and OpenGL renderer are compiled directly from the repository.
A deterministic engine stub supplies display state without touching an audio
device. `generate_sapphire_skin.py` creates the custom redistributable skin at
capture time, so no third-party Winamp artwork is stored in the repository.

Run from anywhere in the repository:

```sh
tools/screenshots/capture.sh
```

Outputs:

- `screenshots/aplay-ember.png`
- `screenshots/aplay-sapphire-skin.png`
- `screenshots/aplay-context-menu.png`

Requirements are a C compiler, Python 3, the runtime `libEGL.so.1`, and a Mesa
EGL implementation with `EGL_PLATFORM_SURFACELESS_MESA`. GLFW headers and
`libglfw` are intentionally not used.
