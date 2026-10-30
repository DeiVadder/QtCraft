// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcWorldInfo.h"

#include <QList>
#include <QString>

/*! \brief What a new world starts with besides its name and seed (QcWorldLibrary::create). */
struct QcNewWorld
{
    /*! Game rule: players keep their inventory when they die. */
    bool keepInventory = true;
    /*! World generator type (QcWorldGenerators) and its options (the layers of a flat world).
        \qcshared */
    QString generator = QString::fromLatin1(QcWorldInfo::defaultGenerator);
    QString options;
    /*! World generation options of a terrain world (decision E-917,
        QcWorldOptions::valuesToStore). */
    QJsonObject worldOptions;
    /*! The version of the world generator the world is generated with for good (decision E-922). */
    int generatorVersion = QcGeneratorVersion::current;
    /*! Game mode of players who join for the first time (decision E-282). */
    QcGameMode gameMode = QcGameMode::Survival;
};

/*! \brief The saved worlds of this installation: one sub-directory per world below \c {root}. */
class QcWorldLibrary
{
public:
    /*! \brief One saved world as the library lists it. */
    struct Entry
    {
        /*! Name of the world's sub-directory below the root. */
        QString folder;
        /*! Path of the world's directory. */
        QString directory;
        /*! The contents of world.json (the folder name as world name when it cannot be read). */
        QcWorldInfo info;
        /*! Set when world.json cannot be read; the world is listed but cannot be played. */
        QString error;
        /*! Saved by an older development version (decision E-356): listed, but it cannot be
            played. */
        bool tooOld = false;
    };

    /*! The setup of a new world (QcNewWorld). */
    using NewWorld = QcNewWorld;

    explicit QcWorldLibrary(QString root);

    /*! Base directory for worlds and mods. QTCRAFT_DATA_DIR overrides the platform's application
        data location (tests, screenshots). */
    static QString dataRoot();
    /*! Returns \c {<data root>/worlds}. */
    static QString defaultRoot();
    /*! Returns the seed of a new world from the menu's \a text: a number as it is, any other text
        hashed (FNV-1a, stable across runs and platforms, unlike qHash). The map preview takes
        the same seed. */
    [[nodiscard]] static qint64 seedFromText(const QString &text);

    /*! Returns the directory that holds the worlds. */
    const QString &root() const { return m_root; }
    /*! Returns the worlds (directories with a world.json), newest first by last played time. */
    QList<Entry> list() const;
    /*! Creates the directory and world.json for a new world called \a name with \a seed and
        \a setup; returns the new entry. The folder name comes from \a name (other characters
        than letters, digits, \c {-} and \c {_} replaced), with a number appended when it is
        taken. The generator's setup runs when the world first opens. */
    [[nodiscard]] QcResult<Entry> create(const QString &name, qint64 seed, const NewWorld &setup = {}) const;
    /*! Deletes the world directory \a folder. Only plain folder names directly below the root are
        accepted. Returns an error when \a folder is no plain name, is a link, holds no
        world.json or cannot be deleted. */
    [[nodiscard]] QcStatus remove(const QString &folder) const;
    /*! Returns the path of the world directory \a folder below the root. */
    QString directoryOf(const QString &folder) const;

private:
    QString m_root;
};
