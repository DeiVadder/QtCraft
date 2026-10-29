# Third-party code

Code and fonts from other projects that QtCraft builds and ships with. Assets (textures, sounds) are listed in `docs/ASSETS.md`: the game has no third-party textures, sounds or music. QtCraft itself is licensed under GPL-3.0-or-later (`LICENSE`). The folder `LICENSES/` holds the licence texts of the repository's files under their SPDX names (REUSE): `GPL-3.0-or-later.txt` (a copy of `LICENSE`), `OFL-1.1.txt` (Pixelify Sans) and `LicenseRef-FSF-verbatim.txt` (the licence texts below). The folder `licenses-third-party/` holds the texts the release packages need and no component brings along itself: `LGPL-3.0-only.txt` for Qt and `LGPL-2.1-or-later.txt` for FFmpeg. Source files carry an SPDX header; `REUSE.toml` gives licence and copyright of the files that cannot carry one (content, data, documentation, images), of the font and of `licenses-third-party/`.

## Qt and FFmpeg

| Component | Version | Where | Used for | Licence | Licence text |
|-----------|---------|-------|----------|---------|--------------|
| Qt 6 (The Qt Company Ltd. and the Qt contributors): modules Core, Concurrent, Network, Gui, Multimedia, Quick, QuickControls2, Qml (with the QML modules QtQuick, Controls, Layouts and the ones they pull in), ShaderTools, LinguistTools and Test at build time only | **6.8.3** (open source, the last open-source release of the 6.8 LTS), built by the CI and release workflows; only these packages are distributed (decision E-932). Packages built with any other Qt are for local tests only and are not passed on. `CMakeLists.txt` requires 6.8 | Installed Qt, not in the repository; the release packages carry its frameworks/DLLs, plugins and QML modules as separate, unmodified shared libraries (macdeployqt, windeployqt) | Everything except gamepads and TLS | GNU LGPL version 3 (Qt also offers GPL 2/3 and a commercial licence; QtCraft uses the **LGPL 3** option) | `licenses-third-party/LGPL-3.0-only.txt` plus `LICENSE` (the GPL 3 it builds on); in the macOS bundle `Contents/Resources/licenses/Qt-LGPL-3.0.txt`, `Qt-GPL-3.0.txt`, `Qt-NOTICE.txt` (with the exact source archive, `qt-everywhere-src-6.8.3.tar.xz` on download.qt.io) and Qt's own SBOM files (`Qt-SBOM/`), in the Windows package `licenses\Qt-*.txt`. A local-only package (Qt not declared as open source) carries a `Qt-NOTICE.txt` without the LGPL and `NOT-FOR-DISTRIBUTION.txt` instead |
| Third-party code inside Qt (libpng, libjpeg, PCRE2, HarfBuzz, double-conversion, TinyCBOR, SQLite in the SQL driver plugin and others) | as in Qt | Built into the Qt libraries and plugins | – | various permissive licences, listed by Qt | Qt's SBOM files (`$QT_DIR/sbom/*.spdx`, copied for the shipped modules into the macOS bundle) and the Qt documentation page "Licenses Used in Qt" |
| FFmpeg (libavcodec, libavformat, libavutil, libswresample, libswscale) | the build that comes with the Qt release (Qt's prebuilt FFmpeg of the shipped Qt 6.8.3) | Shared libraries in `Contents/Frameworks` of the macOS bundle, unmodified, placed there by macdeployqt with Qt Multimedia's FFmpeg plugin; the Windows package uses the Windows Media backend and contains no FFmpeg | Sound output of Qt Multimedia (macOS) | GNU LGPL version 2.1 or later (Qt's LGPL build of FFmpeg) | `licenses-third-party/LGPL-2.1-or-later.txt`; in the macOS bundle `Contents/Resources/licenses/FFmpeg-LGPL-2.1.txt` and the FFmpeg part of `Qt-NOTICE.txt`: Qt 6.8.3 builds FFmpeg **n7.1**, named with its build options in `coin/provisioning/common/unix/install-ffmpeg.sh` of the repository qt/qt5 at the tag `v6.8.3` |

## Other code

| Component | Version | Where | Used for | Licence | Licence text |
|-----------|---------|-------|----------|---------|--------------|
| SDL3 (Simple DirectMedia Layer, libsdl-org/SDL) | `release-3.4.18` (`3rdparty/sdl3-tag.txt`) | Git submodule `3rdparty/SDL`, unchanged; only the joystick, gamepad, HIDAPI, haptic and sensor parts are built, statically linked into the game (decision E-540) | Gamepads (stage 40) | zlib (GPL-compatible) | `3rdparty/SDL/LICENSE.txt`; in the macOS bundle `Contents/Resources/licenses/SDL3-LICENSE.txt` |
| HIDAPI (Alan Ott, Signal 11 Software), part of SDL3 | as in SDL3 | `3rdparty/SDL/src/hidapi`, built into SDL3 (without libusb) | USB/Bluetooth access to gamepads | offered under GPL v3, BSD or the original HIDAPI licence; QtCraft uses the **BSD** option | `3rdparty/SDL/src/hidapi/LICENSE-bsd.txt`; in the macOS bundle `Contents/Resources/licenses/HIDAPI-LICENSE.txt` |
| OpenSSL (OpenSSL Project) | `3.5.9`, an LTS line (`3rdparty/openssl-version.txt`, with the SHA-256 of the release tarball) | Release tarball of openssl/openssl, unchanged, downloaded and checked by `scripts/build-openssl.sh` (Windows: `scripts/windows/build-openssl.ps1`); only the shared libraries libcrypto and libssl are built and shipped (`Contents/Frameworks` in the macOS bundle, `libcrypto-3-x64.dll` and `libssl-3-x64.dll` next to `qtcraft.exe` in the Windows package; Linux builds use the OpenSSL 3 of the system and ship none; decision E-872) | TLS in multiplayer (stage 48): Qt's `openssl` TLS backend, and the key of the server's certificate | Apache License 2.0 (compatible with the GPL version 3) | `LICENSE.txt` of the tarball, copied into every cache entry; in the macOS bundle `Contents/Resources/licenses/OpenSSL-LICENSE.txt`, in the Windows package `licenses\OpenSSL-LICENSE.txt` |
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

OpenSSL: Copyright (c) 1998-2026 The OpenSSL Project Authors, Copyright (c) 1995-1998 Eric A. Young, Tim J. Hudson. The Apache License 2.0 asks that redistributions carry a copy of the licence and keep the notices; OpenSSL 3 has no NOTICE file. QtCraft ships the libraries unmodified. Before every release the OpenSSL security advisories are checked and the pinned version is raised to the newest patch release of its line when one is out (decision E-872).

`scripts/build-openssl.sh` copies OpenSSL's licence into its cache entry, and `scripts/build-sdl.sh` copies the three texts into every SDL3 cache entry (`LICENSE.txt`, `HIDAPI-LICENSE.txt`, `LIBM-NOTICE.txt`; an entry without them is not finished), and `scripts/release.sh` puts them into the bundle (`Contents/Resources/licenses`) and checks that they are there; it does the same for the licence of Pixelify Sans, the game's own `LICENSE`, `THIRD_PARTY.md` and `CREDITS.md` (as `QtCraft-LICENSE`, `QtCraft-THIRD_PARTY.md`, `QtCraft-CREDITS.md`), Qt's and FFmpeg's licence texts and a notice with the Qt version, the FFmpeg libraries and where their sources are (`Qt-NOTICE.txt`), plus Qt's SBOM files of the shipped modules when the Qt installation has them. The Windows package (`scripts/windows/package-windows.ps1`, stage 74.4) carries the same texts in its `licenses` folder (`SDL3-LICENSE.txt`, `HIDAPI-LICENSE.txt`, `SDL3-LIBM-NOTICE.txt`, `OpenSSL-LICENSE.txt`, `PixelifySans-OFL.txt`), plus the Qt DLLs' licence (`Qt-LGPL-3.0.txt`, `Qt-GPL-3.0.txt`) and `Qt-NOTICE.txt` (Qt version and where its source is), and checks that they are there.

## Fonts

| Component | Version | Where | Used for | Licence | Licence text |
|-----------|---------|-------|----------|---------|--------------|
| Pixelify Sans (The Pixelify Sans Project Authors), variable font (weight axis) | as published in google/fonts, `ofl/pixelifysans`, downloaded 2026-10-22 (approved by the human) | `src/app/fonts/PixelifySans.ttf` (renamed from `PixelifySans[wght].ttf`, otherwise unchanged), built into the game's Qt resources | The logo in the main menu only (decision E-852, `GameLogo.qml`) | SIL Open Font License 1.1 (may be bundled with software; the font stays under the OFL) | `src/app/fonts/OFL.txt`, also in the resources; in the macOS bundle `Contents/Resources/licenses/PixelifySans-OFL.txt` |

Pixelify Sans: Copyright 2021 The Pixelify Sans Project Authors. The OFL allows embedding and redistributing the font with software, sold or not, as long as the font is not sold on its own, every copy carries the copyright notice and the licence, and modified versions do not use the reserved font name; the game uses the font unmodified.
