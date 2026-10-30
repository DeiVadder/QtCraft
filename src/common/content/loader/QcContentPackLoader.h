// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcResult.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVersionNumber>

/*! \brief Manifest of a content pack (base game or mod): pack.json in the pack directory. */
struct QcContentPackInfo
{
    /*! The pack id (\c {"id"}, see isValidPackId()); also the namespace of its unqualified
        names. */
    QString id;
    /*! The display name (\c {"name"}, the id when missing; at most 256 characters). */
    QString name;
    /*! The version text (\c {"version"}, required; see parseVersion()). */
    QString version;
    /*! The description (\c {"description"}, optional; at most 4096 characters). */
    QString description;
    /*! Pack ids this pack needs; they are loaded first. */
    QStringList dependencies;
    /*! Optional lowest accepted version per dependency. */
    QHash<QString, QVersionNumber> minimumVersions;
    /*! The directory the manifest was read from. */
    QString directory;
};

/*! \brief A pack loaded into a registry: its manifest and the warnings about the entries it
    skipped. */
struct QcLoadedPack
{
    /*! The manifest of the pack. */
    QcContentPackInfo info;
    /*! The warnings about the entries the pack skipped. */
    QStringList warnings;
};

/*! \brief Reads content packs from disk (or Qt resources) into a registry.

    This is the only place that touches pack files; pack data never gets file-system access of
    its own. */
class QcContentPackLoader
{
public:
    /*! Largest file a pack may contain. */
    static constexpr qint64 maxContentFileBytes = qint64{4} * 1024 * 1024;
    /*! lang/<language><suffix>.json adds to <language> (handbook texts kept apart from names). */
    static constexpr auto handbookLanguageSuffix = ".handbook";

    /*! Returns the manifest pack.json in \a directory, or an error when it cannot be read, its id
        or version is invalid, a text is too long, or a dependency entry is invalid. */
    [[nodiscard]] static QcResult<QcContentPackInfo> readManifest(const QString &directory);
    /*! Returns whether \a id is a valid pack id: lower-case letters, digits, '_', '-', '.',
        starting with a letter or digit, at most 64 characters, no "..". */
    static bool isValidPackId(const QString &id);
    /*! Returns \a text as a dotted version, normalized ("1.0.0" == "1"); null when the text is not
        a plain version (trailing text such as "1.0beta" included). */
    static QVersionNumber parseVersion(const QString &text);

    /*! Returns the bytes of the pack file at \a path: only regular files (no links, devices or
        pipes) up to maxContentFileBytes; an error otherwise or when it cannot be opened. */
    [[nodiscard]] static QcResult<QByteArray> readPackFile(const QString &path);

    /*! Returns the content files load() reads from the pack in \a directory besides pack.json
        (the content folders' *.json, textures/\unicode {0x2A}.png, the settings files in
        worldgen/, weather.json and held.json) that exist, sorted; hidden files and symbolic links
        are not among them. */
    static QStringList contentFiles(const QString &directory);
    /*! Returns the bytes that load() would read from the pack in \a directory, counting every file
        as at least 256 bytes and at most maxContentFileBytes + 1, so a pack can be rejected before
        anything is read (hard links make small packs read large). */
    static qint64 measure(const QString &directory);

    /*! Loads the content folders of the pack in \a directory (shapes/, parts/, materials/,
        blocks/, items/, recipes/, smelting/, sounds/, textures/, lang/, handbook/, structures/,
        worldgen/, ...) into \a registry and returns the pack. Invalid entries are skipped and
        reported in the warnings; the pack itself only fails when the manifest is unusable. It
        does not add the pack reference to the registry. */
    [[nodiscard]] static QcResult<QcLoadedPack> load(const QString &directory, QcContentRegistry &registry);
};
