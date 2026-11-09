# Credits

QtCraft's textures and sounds and its generated music are made by the project itself (procedurally at run time or, for the example texture pack "Storybook", exported and hand-tuned from those textures); see `docs/ASSETS.md`. They are licensed like the code, GPL-3.0-or-later. The music tracks are by Scott Buckley (below).

## Texture packs

| Pack | Author | Licence | Attribution |
|------|--------|---------|-------------|
| QtCraft HD (32×, 64×, 128×) | QtCraft | GPL-3.0-or-later | – |
| Storybook (32×) | QtCraft | GPL-3.0-or-later | – |
| Rubies HD (example mod) | QtCraft | GPL-3.0-or-later | – |

Third-party packs added later are listed here with the attribution their licence asks for; the game also shows each pack's `license` and `credits` in Settings → Texture packs.

## Music

The music tracks in `content/qtcraft/music` (Ogg Vorbis, converted from the artist's MP3 files) are licensed under the Creative Commons Attribution 4.0 International licence (CC BY 4.0, https://creativecommons.org/licenses/by/4.0/; text in `LICENSES/CC-BY-4.0.txt` and, in the release packages, `CC-BY-4.0.txt` in the licence folder). The game shows these lines in the handbook chapter "Music" and in Settings → About:

- 'Borealis' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Cobalt' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Ephemera' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Hymn To The Dawn' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'In Search Of Solitude' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'In This Moment' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Intervention' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Katabasis I (The Weight of Air)' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Machina' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Moonlight' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Shadows and Dust' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'She Moved Mountains' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'The Old Ones' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au
- 'Unraveling' by Scott Buckley - released under CC-BY 4.0. www.scottbuckley.com.au

## Fonts

| Font | Author | Licence | Use |
|------|--------|---------|-----|
| Pixelify Sans | The Pixelify Sans Project Authors (Copyright 2021) | SIL Open Font License 1.1 (`src/app/fonts/OFL.txt`) | The QtCraft logo in the main menu (see `THIRD_PARTY.md`) |

## Libraries

| Library | Author | Licence | Use |
|---------|--------|---------|-----|
| stb_vorbis | Sean Barrett | public domain (Unlicense; also offered under MIT) | Decoding the Ogg Vorbis music (see `THIRD_PARTY.md`) |
| SDL3 | Sam Lantinga and the SDL contributors | zlib | Gamepad input, statically linked (see `THIRD_PARTY.md`) |
| HIDAPI (in SDL3) | Alan Ott, Signal 11 Software | BSD | Gamepad access over USB and Bluetooth, inside SDL3 (see `THIRD_PARTY.md`) |
| fdlibm maths routines (in SDL3) | Sun Microsystems (SunPro) | permissive notice | SDL's maths functions (see `THIRD_PARTY.md`) |
| OpenSSL 3 | The OpenSSL Project Authors | Apache-2.0 | TLS in multiplayer and the server's key, shipped as shared libraries (see `THIRD_PARTY.md`) |
| Qt 6 | The Qt Company and the Qt contributors | LGPL-3.0 | Everything else, shipped as shared libraries |
| FFmpeg (with Qt Multimedia, macOS) | The FFmpeg developers | LGPL-2.1-or-later | Sound output on macOS, shipped as shared libraries (see `THIRD_PARTY.md`) |
