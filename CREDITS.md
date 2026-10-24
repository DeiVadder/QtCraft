# Credits

QtCraft's textures, sounds and music are made by the project itself (procedurally at run time or, for the example texture pack "Storybook", exported and hand-tuned from those textures); see `docs/ASSETS.md`. They are licensed like the code, GPL-3.0-or-later.

## Texture packs

| Pack | Author | Licence | Attribution |
|------|--------|---------|-------------|
| QtCraft HD (32×, 64×, 128×) | QtCraft | GPL-3.0-or-later | – |
| Storybook (32×) | QtCraft | GPL-3.0-or-later | – |
| Rubies HD (example mod) | QtCraft | GPL-3.0-or-later | – |

Third-party packs added later are listed here with the attribution their licence asks for; the game also shows each pack's `license` and `credits` in Settings → Texture packs.

## Fonts

| Font | Author | Licence | Use |
|------|--------|---------|-----|
| Pixelify Sans | The Pixelify Sans Project Authors (Copyright 2021) | SIL Open Font License 1.1 (`src/app/fonts/OFL.txt`) | The QtCraft logo in the main menu (see `THIRD_PARTY.md`) |

## Libraries

| Library | Author | Licence | Use |
|---------|--------|---------|-----|
| SDL3 | Sam Lantinga and the SDL contributors | zlib | Gamepad input, statically linked (see `THIRD_PARTY.md`) |
| HIDAPI (in SDL3) | Alan Ott, Signal 11 Software | BSD | Gamepad access over USB and Bluetooth, inside SDL3 (see `THIRD_PARTY.md`) |
| fdlibm maths routines (in SDL3) | Sun Microsystems (SunPro) | permissive notice | SDL's maths functions (see `THIRD_PARTY.md`) |
| OpenSSL 3 | The OpenSSL Project Authors | Apache-2.0 | TLS in multiplayer and the server's key, shipped as shared libraries (see `THIRD_PARTY.md`) |
| Qt 6 | The Qt Company and the Qt contributors | LGPL-3.0 | Everything else, shipped as shared libraries |
