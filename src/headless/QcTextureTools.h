// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

class QcContentRegistry;

/*! \brief Texture pack tools of qtcraft-headless (stage 28): exporting the content's textures as
    an image texture pack (a starting point for pack authors and how the example pack was made,
    E-303) and measuring the atlas of the shipped packs (size, memory, build time; E-300). */
namespace QcTextureTools {

/*! \brief What exportPack() writes (the options of --export-textures). */
struct ExportRequest
{
    /*! The directory the pack is written into. */
    QString directory;
    /*! Tile size in pixels: a power of two from 16 to 512. */
    int resolution = 32;
    /*! Packs drawn over the content, the first with the highest priority
        ("builtin:qtcraft_hd_32"). */
    QStringList packs;
    /*! Only these texture names ("qtcraft:stone"); all when empty. */
    QStringList keys;
    /*! The pack's name in the manifest; empty for \c {Exported textures <resolution>x}. */
    QString name;
    /*! The mods directory whose texture packs \c {packs} may name too. */
    QString modsDirectory;
};

/*! Writes the textures of \a content as \a request asks: texturepack.json,
    textures/<namespace>/<name>.png and preview.png. Returns the exit code (0 on success, else
    1). */
int exportPack(const QcContentRegistry &content, const ExportRequest &request);

/*! Builds the atlas of \a content without packs and with every built-in pack (\a modsDirectory
    joins the pack search) and prints one line per pack: requested and used tile size, atlas size,
    mip levels, memory, load and build time (best of three). Returns 0. */
int printAtlasStats(const QcContentRegistry &content, const QString &modsDirectory);

} // namespace QcTextureTools
