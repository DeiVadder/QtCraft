#pragma once

#include "QcContentRegistry.h"
#include "QcResult.h"

#include <QString>
#include <QStringList>

// Manifest of a content pack (base game or mod): pack.json in the pack directory.
struct QcContentPackInfo
{
    QString id;
    QString name;
    QString version;
    QString description;
    QStringList dependencies;
    QString directory;
};

// Reads content packs from disk (or Qt resources) into a registry. This is the only place that
// touches pack files; pack data never gets file-system access of its own.
class QcContentPackLoader
{
public:
    static QcResult<QcContentPackInfo> readManifest(const QString &directory);

    // Loads blocks/, textures/ and lang/ of the pack. Invalid entries are skipped and reported in
    // `warnings`; the pack itself only fails when the manifest is unusable.
    static QcResult<QcContentPackInfo> load(const QString &directory, QcContentRegistry &registry, QStringList &warnings);
};
