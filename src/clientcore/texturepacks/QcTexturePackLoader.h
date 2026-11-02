// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePack.h"
#include "common/content/blocks/QcTextureAnimation.h"

#include <QByteArray>
#include <QImage>
#include <QList>

#include <expected>

/*! \brief Reads a texture pack folder (decision E-297) and checks everything in it at the system
    boundary (decision E-298): the manifest, file names (only
    \c {textures/<namespace>/<name>.png|json}, no links, no hidden files, so nothing outside the
    folder is read), the number and size of files, PNG signature, size and resolution, damaged
    images and a budget for decoded image memory.

    A bad file is skipped with a problem and its texture falls back to the next pack; only a
    missing or invalid manifest makes the whole pack unusable. */
namespace QcTexturePackLoader {

/*! \brief Limits a pack must keep, checked while loading. */
struct Limits
{
    /*! Files read below textures/. */
    int maxFiles = 2048;
    /*! Namespace folders read below textures/. */
    int maxNamespaces = 64;
    /*! Problems kept per pack; the rest is counted in one line. */
    int maxProblems = 100;
    /*! Largest PNG file in bytes. */
    qint64 maxImageFileBytes = qint64{4} * 1024 * 1024;
    /*! Largest procedural spec (.json) in bytes. */
    qint64 maxSpecFileBytes = qint64{64} * 1024;
    /*! Decoded RGBA bytes of all images of one pack. */
    qint64 memoryBudget = qint64{192} * 1024 * 1024;
    /*! Images above this edge length are stored scaled down (the atlas never uses more, E-300). */
    int maxStoredResolution = 256;
    /*! Vertical strips of animation frames (decision E-878): at most this many frames. */
    int maxFrames = QcTextureAnimation::maxFrames;
    /*! The labPBR material maps \c {<name>_n.png}, \c {<name>_s.png} (decision E-908): read only
        when the realistic mode draws them, after the colour textures and with their own file
        limit and budget, so they never push a colour texture out; stored at most mapResolution
        wide. */
    bool materialMaps = false;
    /*! Material map files read. */
    int maxMapFiles = 4096;
    /*! Decoded RGBA bytes of all material maps of one pack. */
    qint64 mapMemoryBudget = qint64{192} * 1024 * 1024;
    /*! Largest edge length a material map is stored at. */
    int mapResolution = 128;
};

/*! \brief A checked PNG texture: the first frame at the pack's resolution and how many frames
    the file had, the frames after the first at the same size (a vertical strip animates,
    decision E-878). \qcfields */
struct Image
{
    QImage image;
    int frames = 1;
    QList<QImage> laterFrames;
};

/*! Returns the manifest of the pack folder \a directory alone (for listing packs without loading
    their images). */
std::expected<QcTexturePackManifest, QcTexturePackProblem> readManifest(const QString &directory);

/*! Returns the pack in \a directory with \a id and \a origin, checked against \a limits; the error
    when its manifest is missing or invalid. */
std::expected<QcTexturePack, QcTexturePackProblem> load(const QString &directory, const QString &id, QcTexturePack::Origin origin,
                                                        const Limits &limits = {});

/*! Returns the preview image \a manifest names in \a directory (at most 512 x 512 pixels, within \a
    limits); a null image when it names none. */
std::expected<QImage, QcTexturePackProblem> readPreview(const QString &directory, const QcTexturePackManifest &manifest, const Limits &limits = {});

/*! Returns the image of one PNG file's \a bytes, checked against the pack's \a resolution and
    \a limits. */
std::expected<Image, QcTexturePackProblem> readImage(const QByteArray &bytes, int resolution, const Limits &limits = {});

} // namespace QcTexturePackLoader
