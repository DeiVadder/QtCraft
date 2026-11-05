// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "clientcore/texturepacks/QcTexturePackProblem.h"

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <expected>

/*! \brief The manifest \c {texturepack.json} of a texture pack (decision E-297): what the
    settings show and the resolution every image of the pack must have. */
struct QcTexturePackManifest
{
    /*! Name of the manifest file in the pack folder. */
    static constexpr auto fileName = "texturepack.json";
    /*! The only manifest format this game reads ("format"). */
    static constexpr int formatVersion = 1;
    /*! Smallest texture resolution of a pack. */
    static constexpr int minResolution = 16;
    /*! Largest texture resolution of a pack. */
    static constexpr int maxResolution = 512;
    /*! Largest size of the manifest file in bytes. */
    static constexpr qint64 maxBytes = qint64{64} * 1024;
    /*! Longest name (also of the preview file); the licence and each credit's author, license,
        source and note may be four times as long. */
    static constexpr int maxNameLength = 64;
    /*! Longest description, in each language. */
    static constexpr int maxTextLength = 1000;
    /*! Most entries in "credits". */
    static constexpr int maxCredits = 32;
    /*! Most languages in "descriptions". */
    static constexpr int maxDescriptions = 32;

    /*! \brief Who made (part of) the pack, for packs with material of several authors. */
    struct Credit
    {
        /*! Author of the material ("author"). */
        QString author;
        /*! Licence of the material ("license"). */
        QString license;
        /*! Where the material comes from ("source"). */
        QString source;
        /*! Free text ("note"). */
        QString note;

        friend bool operator==(const Credit &, const Credit &) = default;
    };

    /*! Manifest format ("format"). */
    int format = formatVersion;
    /*! Name of the pack shown in the settings. */
    QString name;
    /*! Description shown when the game's language has none of its own in descriptions. */
    QString description;
    /*! The description in other languages, by language code ("de"; keys read in lower case up to
        "_" or "-"), from the optional object "descriptions"; the settings show the one of the
        game's language (TP-2 of docs/Menue-Pruefung.md). */
    QHash<QString, QString> descriptions;
    /*! Edge length of one texture in pixels: a power of two from 16 to 512. */
    int resolution = minResolution;
    /*! Licence of the pack as a whole (e.g. "CC0-1.0", "CC-BY-4.0"). */
    QString license;
    /*! Who made parts of the pack (optional "credits"). */
    QList<Credit> credits;
    /*! Renders the procedural textures of the game and of mods
        (content/\unicode {0x2A}/textures/\unicode {0x2A}.json) at the pack's resolution, below
        the pack's own files. */
    bool procedural = false;
    /*! Optional preview image in the pack folder: a plain file name ending in .png. */
    QString preview;

    /*! Returns the description in \a language ("de"), the plain one without a text in that
        language. */
    QString descriptionIn(const QString &language) const { return descriptions.value(language, description); }

    /*! Returns the manifest read from \a bytes after checking every field; the first problem
        found is the error. */
    static std::expected<QcTexturePackManifest, QcTexturePackProblem> parse(const QByteArray &bytes);
    /*! Returns whether \a resolution is a power of two from minResolution to maxResolution. */
    static bool isValidResolution(int resolution);
    /*! Returns whether \a name is a file name inside the pack folder: no path separators, no
        "..", no hidden file. */
    static bool isPlainFileName(const QString &name);

    friend bool operator==(const QcTexturePackManifest &, const QcTexturePackManifest &) = default;
};
