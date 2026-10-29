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

// A loaded texture pack (decision E-297): its manifest and the textures it brings, keyed by the
// content's texture name ("qtcraft:stone"). Images are checked and decoded at load time and never
// change afterwards, so a pack is shared between threads as std::shared_ptr<const QcTexturePack>.
struct QcTexturePack
{
    // Where a pack comes from; part of its id.
    enum class Origin {
        BuiltIn,
        User,
        Mod,
    };

    // "builtin:<folder>", "user:<folder>" or "mod:<mod folder>/<folder>".
    QString id;
    Origin origin = Origin::User;
    QString directory;
    QcTexturePackManifest manifest;
    // PNG textures at the pack's resolution (larger ones stored at maxStoredResolution).
    QHash<QString, QImage> images;
    // The frames after the first of the images that are vertical strips of animation frames
    // (decision E-878), at the size of their first frame.
    QHash<QString, QList<QImage>> laterFrames;
    // The labPBR material maps of the realistic mode (decision E-908) by the texture they belong to:
    // `<name>_n.png` (normal, occlusion, height) and `<name>_s.png` (smoothness, reflectance or
    // metal, porosity, emission), at the pack's resolution.
    QHash<QString, QImage> normalMaps;
    QHash<QString, QImage> specularMaps;
    // Material map files the pack has that were not read (loaded without the maps): the pack must
    // be read again when the maps are wanted (decision E-912).
    qsizetype unreadMaps = 0;
    // Procedural definitions (the fields of content/*/textures/*.json).
    QHash<QString, QJsonObject> specs;
    // Optional preview from the manifest's "preview".
    QImage preview;
    QcTexturePackProblems problems;
    // Bytes of all decoded images (checked against the memory budget while loading).
    qint64 decodedBytes = 0;

    enum class MapKind { Normal, Specular };
    // Whether the image key ("qtcraft:stone_n") names a material map: the suffixes `_n` and `_s`
    // are reserved for them.
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

    bool provides(const QString &key) const { return images.contains(key) || specs.contains(key); }
    // Whether the pack holds material maps read from its files.
    bool holdsMaps() const { return !normalMaps.isEmpty() || !specularMaps.isEmpty(); }
};

using QcTexturePackRef = std::shared_ptr<const QcTexturePack>;
// Active packs, the highest priority first.
using QcTexturePackStack = QList<QcTexturePackRef>;
