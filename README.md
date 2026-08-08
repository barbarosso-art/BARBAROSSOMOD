# BARBAROSSOMOD v0.1

BARBAROSSOMOD displays a background image supplied by the currently selected
custom Beat Saber map. The image is active only for that map and does not affect
the menu or other songs.

## Supported versions

- PC: Beat Saber `1.40.8`
- Quest: Beat Saber `1.40.8_7379`

## Downloads

- PC: `Release/BARBAROSSOMOD_v0.1_PC.zip`
- Quest: `Release/BARBAROSSOMOD_v0.1.qmod`

## Installation

### PC

Install BSIPA `4.3.6` and SiraUtil `3.0.5`, then extract the PC ZIP into the
Beat Saber instance root. `BARBAROSSOMOD.dll` and `MapImagePCAssets.bundle` must
be placed in `Plugins`.

### Quest

Use a modded Beat Saber `1.40.8_7379`. Upload `BARBAROSSOMOD_v0.1.qmod` through
ModsBeforeFriday. The QMOD declares beatsaber-hook, BSML, custom-types and paper2
dependencies. Maps may additionally require SongCore and Chroma.

## Map format

Add the following block to `_customData` in `Info.dat` and place the referenced
PNG/JPG/JPEG next to `Info.dat`:

```json
"_mapImage": {
  "_enabled": true,
  "_file": "Background.png",
  "_position": [0.0, 11.0, 45.0],
  "_rotation": [0.0, 0.0, 0.0],
  "_scale": [3200.0, 2133.0, 1.0],
  "_showInMenu": false,
  "_showInMap": true
}
```

Only a single safe filename is accepted. Paths containing `/`, `\\` or `..` are
rejected. If the block is missing or `_enabled` is `false`, no image is created.

## Building

- PC: run `PC/build.ps1` with a Beat Saber 1.40.8 BSManager instance available.
- Quest: install QPM, Ninja, CMake and Android NDK 27, then run `qpm restore` and
  `qpm s qmod` from `Quest`.

## Credits

- BARBAROSSO — map-owned background design, PC implementation and Quest adaptation.
- vcmikuu — original Imager project used as the basis of the Quest implementation.

See `THIRD_PARTY_PERMISSION.md` and `LICENSE.md`.

