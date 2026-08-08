# BARBAROSSOMOD v0.1 — Quest source

`BARBAROSSOMOD` loads a background image from the currently selected custom map.
The image path and transform are declared in `_customData._mapImage` in `Info.dat`.
Nothing is shown for maps without that block.

Target: Beat Saber Quest `1.40.8_7379`.

## Build

- `qpm s build` — compile the native library.
- `qpm s qmod` — create `BARBAROSSOMOD_v0.1.qmod`.

On Windows keep the CMake source and NDK paths ASCII-only. Cyrillic characters in
the source or NDK path can prevent the Android linker from starting.

See `README_MAP_IMAGE.md` for the map format, installation and upstream attribution.
