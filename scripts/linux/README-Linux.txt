QtCraft @VERSION@ for Linux (@ARCH@), commit @COMMIT@
======================================================

Starting
--------
QtCraft for Linux is one AppImage file: QtCraft-@VERSION@-Linux-@ARCH@.AppImage. Nothing is
installed and no administrator rights are needed.

1. Make the file executable, once:

       chmod +x QtCraft-@VERSION@-Linux-@ARCH@.AppImage

   (or in the file manager: Properties, Permissions, "Allow executing file as program").
2. Start it by double click or from a terminal:

       ./QtCraft-@VERSION@-Linux-@ARCH@.AppImage

Requirements: a 64-bit Linux with glibc 2.39 or newer (for example Ubuntu 24.04, Debian 13,
Fedora 40 or newer), X11 or Wayland, a graphics driver with OpenGL (Mesa is fine), and OpenSSL 3
(libssl.so.3 and libcrypto.so.3; every current distribution has it installed). The AppImage
brings Qt 6.12.0 with its ICU, QML modules and plugins, the FFmpeg libraries of Qt Multimedia, the
xcb-util libraries of Qt's X11 plugin and libbz2. Everything else comes from the system: glibc and
the C++ runtime (libstdc++, libgcc_s), OpenGL/EGL and libdrm, X11 and xcb (libX11, libxcb and its
extensions, libXext, libXrandr, libSM, libICE), xkbcommon, Wayland (wayland-client, -cursor,
-egl), fontconfig and freetype, zlib, zstd, brotli, glib, D-Bus, the PulseAudio client (libpulse;
PipeWire provides it too) and OpenSSL 3. A desktop installation has all of them.

FUSE: the AppImage mounts itself through FUSE (the program fusermount3 or fusermount, which
desktop distributions install; libfuse.so.2 is not needed). If it cannot mount itself (for
example in a container), install FUSE (Debian/Ubuntu: sudo apt install fuse3, Fedora:
sudo dnf install fuse3) or start the AppImage without FUSE:

    ./QtCraft-@VERSION@-Linux-@ARCH@.AppImage --appimage-extract-and-run

(or unpack it once with --appimage-extract and start squashfs-root/AppRun).

Encryption: multiplayer connections are encrypted with TLS through the OpenSSL 3 of the system.
Keep your distribution's OpenSSL up to date; the AppImage contains no OpenSSL of its own.

Single player
-------------
Create a world in the main menu and play. The server runs inside the game, without network.

Multiplayer
-----------
Main menu, "Multiplayer", enter the server address, choose a player name, enter a password only
if the host names one, then "Join". Game and server must be the same version.

Dedicated server
----------------
The same AppImage contains the dedicated server qtcraft-headless. Give --headless as the first
argument:

    ./QtCraft-@VERSION@-Linux-@ARCH@.AppImage --headless --help
    ./QtCraft-@VERSION@-Linux-@ARCH@.AppImage --headless --server --console --port 25600

or call it through a link named qtcraft-headless:

    ln -s QtCraft-@VERSION@-Linux-@ARCH@.AppImage qtcraft-headless
    ./qtcraft-headless --server --console

On a server without FUSE add --appimage-extract-and-run before --headless, or unpack the AppImage
once (--appimage-extract) and run squashfs-root/AppRun --headless ... from there. The server
stops and saves its world on SIGTERM (systemctl stop) or with /stop on its console.

Data, logs and crash reports
----------------------------
Worlds, settings, mods and logs are in ~/.local/share/QtCraft/QtCraft (logs/ holds the logs and
crash reports; Settings, Debug, "Open log folder" opens it). Settings, Debug, "Collect
diagnostics" gathers a folder to send to the maintainers.

Licences
--------
QtCraft is licensed under the GNU GPL version 3 or later. The AppImage contains Qt 6.12.0, used
under the GNU LGPL version 3, and the FFmpeg libraries of Qt Multimedia (GNU LGPL version 2.1 or
later). The licence texts and a notice with the corresponding sources are inside the AppImage in
usr/share/doc/qtcraft/licenses: unpack it with --appimage-extract and look in
squashfs-root/usr/share/doc/qtcraft/licenses (also: SDL3, HIDAPI, SDL libm, the font Pixelify
Sans, THIRD_PARTY.md and CREDITS.md). The folder usr/bin/mods holds the example mod "rubies"; the
game copies it switched off into its data folder on the first start.

Deutsch (kurz)
--------------
Einmal "chmod +x QtCraft-@VERSION@-Linux-@ARCH@.AppImage", dann starten. Fehlt FUSE
(fusermount3), mit --appimage-extract-and-run starten oder fuse3 installieren. OpenSSL 3 kommt
vom System. Den eigenständigen Server startet "--headless" als erstes Argument (oder ein Link namens
qtcraft-headless). Lizenzen: mit --appimage-extract entpacken, Ordner
squashfs-root/usr/share/doc/qtcraft/licenses.
