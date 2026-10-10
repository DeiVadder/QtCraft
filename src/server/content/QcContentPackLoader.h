#pragma once

#include "QcContentRegistry.h"
#include "QcResult.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

// Manifest of a content pack (base game or mod): pack.json in the pack directory.
struct QcContentPackInfo
{
    QString id;
    QString name;
    QString version;
    QString description;
    // Pack ids this pack needs; they are loaded first.
    QStringList dependencies;
    // Optional lowest accepted version per dependency.
    QHash<QString, QVersionNumber> minimumVersions;
    QString directory;
};

// Reads content packs from disk (or Qt resources) into a registry. This is the only place that
// touches pack files; pack data never gets file-system access of its own.
class QcContentPackLoader
{
public:
    // Largest file a pack may contain.
    static constexpr qint64 maxContentFileBytes = qint64(4) * 1024 * 1024;
    // lang/<language><suffix>.json adds to <language> (handbook texts kept apart from names).
    static constexpr auto handbookLanguageSuffix = ".handbook";

    static QcResult<QcContentPackInfo> readManifest(const QString &directory);
    // Lower-case letters, digits, '_', '-', '.', at most 64 characters, no "..".
    static bool isValidPackId(const QString &id);
    // Dotted version, normalized ("1.0.0" == "1"); null when the text is not a plain version.
    static QVersionNumber parseVersion(const QString &text);

    // Reads one file of a pack: only regular files (no links, devices or pipes) up to
    // maxContentFileBytes.
    static QcResult<QByteArray> readPackFile(const QString &path);

    // Bytes that load() would read from the pack, counting every file as at least 256 bytes, so a
    // pack can be rejected before anything is read (hard links make small packs read large).
    static qint64 measure(const QString &directory);

    // Loads the content folders of the pack (shapes/, parts/, materials/, blocks/, items/, recipes/,
    // smelting/, sounds/, textures/, lang/, handbook/, structures/, worldgen/, ...). Invalid entries are skipped and reported in
    // `warnings`; the pack itself only fails when the manifest is unusable.
    static QcResult<QcContentPackInfo> load(const QString &directory, QcContentRegistry &registry, QStringList &warnings);
};
