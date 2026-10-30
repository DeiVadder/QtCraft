# Third-party assets

QtCraft contains one third-party asset, the logo's pixel font Pixelify Sans (SIL Open Font License 1.1, `src/app/fonts`, listed in [THIRD_PARTY.md](../THIRD_PARTY.md) and [CREDITS.md](../CREDITS.md)). The game's textures are generated procedurally at runtime from the content definitions (`content/*/textures/*.json`); the only image files are the project's own texture pack files below (exported from those textures and hand-tuned). All sounds are synthesized from `content/*/sounds/*.json` and the music is composed algorithmically at runtime; the repository contains no audio files.

## Third-party code

Code of other projects is listed in [THIRD_PARTY.md](../THIRD_PARTY.md) with version, licence and where the licence text is shipped: Qt (LGPL 3) with the FFmpeg libraries of Qt Multimedia (LGPL 2.1 or later), SDL3 for gamepads (zlib licence, with HIDAPI under its BSD option and SunPro's maths routines) and OpenSSL 3 for encrypted multiplayer (Apache License 2.0).

## Texture packs

The built-in texture packs are made by the project itself, without downloads. They are licensed like the code, GPL-3.0-or-later (decision E-303); the manifests carry the SPDX id `GPL-3.0-or-later`:

| Pack | Files | Made how | Licence |
|------|-------|----------|---------|
| `content/texturepacks/qtcraft_hd_32`, `_64`, `_128` | `texturepack.json`, `preview.png` | Manifest only; the textures are painted at run time from the procedural definitions; the preview is exported by `scripts/make-texture-packs.sh` | GPL-3.0-or-later |
| `content/texturepacks/storybook_32` | 62 PNG textures, `preview.png` | Exported from the procedural textures at 32 × 32 (`qtcraft-headless --export-textures`), then colour-graded, outlined and given hand-placed details by `scripts/texturepack-style.py` | GPL-3.0-or-later |
| `mods/rubies/texturepacks/rubies_hd` | 3 procedural specs | Written by hand | GPL-3.0-or-later |

### Adding a third-party pack

Only CC0/public domain or permissive licences that allow redistribution (e.g. CC-BY with attribution); no Minecraft textures or packs derived from them; only image files, no executables or scripts; check archive contents before unpacking. For every pack add a row here and an entry in `CREDITS.md`, and fill the pack's `license` and `credits` in its `texturepack.json` so the settings show them:

| Pack folder | Files | Source (URL) | Author | Licence | Date | Changes |
|-------------|-------|--------------|--------|---------|------|---------|
| – | – | – | – | – | – | – |
