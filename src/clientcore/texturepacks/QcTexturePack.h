#pragma once

#include "clientcore/texturepacks/QcTexturePackManifest.h"
#include "clientcore/texturepacks/QcTexturePackProblem.h"

#include <QHash>
#include <QImage>
#include <QJsonObject>
#include <QString>

#include <memory>

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
    // Procedural definitions (the fields of content/*/textures/*.json).
    QHash<QString, QJsonObject> specs;
    // Optional preview from the manifest's "preview".
    QImage preview;
    QcTexturePackProblems problems;
    // Bytes of all decoded images (checked against the memory budget while loading).
    qint64 decodedBytes = 0;

    bool provides(const QString &key) const { return images.contains(key) || specs.contains(key); }
};

using QcTexturePackRef = std::shared_ptr<const QcTexturePack>;
// Active packs, the highest priority first.
using QcTexturePackStack = QList<QcTexturePackRef>;
