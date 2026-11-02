// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePackManifest.h"
#include "clientcore/texturepacks/QcTexturePackProblem.h"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <memory>
#include <optional>

/*! \brief A loaded texture pack (decision E-297): its manifest and the textures it brings, keyed by
    the content's texture name ("qtcraft:stone").

    Images are checked and decoded at load time and never change afterwards, so a pack is shared
    between threads as std::shared_ptr<const QcTexturePack>. */
struct QcTexturePack
{
    /*! \brief Where a pack comes from; part of its id. */
    enum class Origin {
        BuiltIn, // Shipped with the game
        User,    // In the user's texture pack folder
        Mod,     // Brought by a mod
    };

    /*! Id of the pack: "builtin:<folder>", "user:<folder>" or "mod:<mod folder>/<folder>". */
    QString id;
    /*! Where the pack comes from. */
    Origin origin = Origin::User;
    /*! Absolute path of the pack's folder. */
    QString directory;
    /*! The pack's manifest. */
    QcTexturePackManifest manifest;
    /*! PNG textures at the pack's resolution (larger ones stored at maxStoredResolution). */
    QHash<QString, QImage> images;
    /*! The frames after the first of the images that are vertical strips of animation frames
        (decision E-878), at the size of their first frame. */
    QHash<QString, QList<QImage>> laterFrames;
    /*! The labPBR material maps of the realistic mode (decision E-908) by the texture they
        belong to: \c {<name>_n.png} (normal, occlusion, height) and \c {<name>_s.png}
        (smoothness, reflectance or metal, porosity, emission), at the pack's resolution.
        \qcshared */
    QHash<QString, QImage> normalMaps;
    QHash<QString, QImage> specularMaps;
    /*! Material map files the pack has that were not read (loaded without the maps): the pack
        must be read again when the maps are wanted (decision E-912). */
    qsizetype unreadMaps = 0;
    /*! Procedural definitions (the fields of
        content/\unicode {0x2A}/textures/\unicode {0x2A}.json). */
    QHash<QString, QJsonObject> specs;
    /*! Optional preview from the manifest's "preview". */
    QImage preview;
    /*! Problems found while loading, for the player. */
    QcTexturePackProblems problems;
    /*! Bytes of all decoded images (checked against the memory budget while loading). */
    qint64 decodedBytes = 0;

    /*! Kind of a material map: normal or specular. */
    enum class MapKind {
        Normal,   // _n: normal, occlusion, height
        Specular  // _s: smoothness, reflectance or metal, porosity, emission
    };
    /*! Returns whether the image \a key ("qtcraft:stone_n") names a material map: the suffixes
        \c {_n} and \c {_s} are reserved for them. */
    static std::optional<MapKind> materialMapOf(const QString &key)
    {
        if (key.endsWith(QLatin1StringView("_n"))) {
            return MapKind::Normal;
        }
        if (key.endsWith(QLatin1StringView("_s"))) {
            return MapKind::Specular;
        }
        return std::nullopt;
    }

    /*! Returns whether the pack has an image or a procedural spec for \a key. */
    bool provides(const QString &key) const { return images.contains(key) || specs.contains(key); }
    /*! Returns whether the pack holds material maps read from its files. */
    bool holdsMaps() const { return !normalMaps.isEmpty() || !specularMaps.isEmpty(); }
};

/*! Shared handle to a loaded pack; packs never change after loading. */
using QcTexturePackRef = std::shared_ptr<const QcTexturePack>;
/*! Active packs, the highest priority first. */
using QcTexturePackStack = QList<QcTexturePackRef>;
