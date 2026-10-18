# Third-party code

Code from other projects that QtCraft builds and ships with. Assets (textures, sounds) are listed in `docs/ASSETS.md`; Qt, which the release packages contain as shared libraries, is used under the GNU LGPL version 3 (see `README.md`).

| Component | Version | Where | Used for | Licence | Licence text |
|-----------|---------|-------|----------|---------|--------------|
| SDL3 (Simple DirectMedia Layer, libsdl-org/SDL) | `release-3.4.18` (`3rdparty/sdl3-tag.txt`) | Git submodule `3rdparty/SDL`, unchanged; only the joystick, gamepad, HIDAPI, haptic and sensor parts are built, statically linked into the game (decision E-540) | Gamepads (stage 40) | zlib (GPL-compatible) | `3rdparty/SDL/LICENSE.txt`; in the macOS bundle `Contents/Resources/licenses/SDL3-LICENSE.txt` |
| HIDAPI (Alan Ott, Signal 11 Software), part of SDL3 | as in SDL3 | `3rdparty/SDL/src/hidapi`, built into SDL3 (without libusb) | USB/Bluetooth access to gamepads | offered under GPL v3, BSD or the original HIDAPI licence; QtCraft uses the **BSD** option | `3rdparty/SDL/src/hidapi/LICENSE-bsd.txt`; in the macOS bundle `Contents/Resources/licenses/HIDAPI-LICENSE.txt` |
| Maths routines of SunPro (fdlibm), part of SDL3 | as in SDL3 | `3rdparty/SDL/src/libm`, built into SDL3 | SDL's own sine, cosine, power and similar functions | permissive notice (below) | notice in every file of `3rdparty/SDL/src/libm`; in the macOS bundle `Contents/Resources/licenses/SDL3-LIBM-NOTICE.txt` |

SDL3: Copyright (C) 1997-2026 Sam Lantinga. The zlib licence asks that the origin is not misrepresented and that its notice stays in source distributions; the submodule keeps it unchanged, and the release bundle carries a copy as acknowledgment.

HIDAPI: Copyright (c) 2010, Alan Ott, Signal 11 Software. All rights reserved. The BSD licence asks that binary redistributions reproduce the copyright notice, the conditions and the disclaimer in the documentation; the release bundle carries its text.

The SunPro maths routines carry this notice, which is preserved here and in the release bundle:

```
Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.

Developed at SunPro, a Sun Microsystems, Inc. business.
Permission to use, copy, modify, and distribute this
software is freely granted, provided that this notice
is preserved.
```

`scripts/build-sdl.sh` copies the three texts into every SDL3 cache entry (`LICENSE.txt`, `HIDAPI-LICENSE.txt`, `LIBM-NOTICE.txt`; an entry without them is not finished), and `scripts/release.sh` puts them into the bundle and checks that they are there.
