#pragma once

#include "common/core/QcResult.h"
#include "common/world/QcWorldInfo.h"

#include <QList>
#include <QString>

// What a new world starts with besides its name and seed (QcWorldLibrary::create).
struct QcNewWorld
{
    bool keepInventory = true;
    // World generator type (QcWorldGenerators) and its options (the layers of a flat world).
    QString generator = QString::fromLatin1(QcWorldInfo::defaultGenerator);
    QString options;
    // World generation options of a terrain world (decision E-917, QcWorldOptions::valuesToStore).
    QJsonObject worldOptions;
    // Game mode of players who join for the first time (decision E-282).
    QcGameMode gameMode = QcGameMode::Survival;
};

// The saved worlds of this installation: one sub-directory per world below `root`.
class QcWorldLibrary
{
public:
    struct Entry
    {
        QString folder;
        QString directory;
        QcWorldInfo info;
        // Set when world.json cannot be read; the world is listed but cannot be played.
        QString error;
        // Saved by an older development version (decision E-356): listed, but it cannot be played.
        bool tooOld = false;
    };

    using NewWorld = QcNewWorld;

    explicit QcWorldLibrary(QString root);

    // Base directory for worlds and mods. QTCRAFT_DATA_DIR overrides the platform's application data
    // location (tests, screenshots).
    static QString dataRoot();
    // <data root>/worlds
    static QString defaultRoot();
    // The seed of a new world from the menu's text: a number as it is, any other text hashed (FNV-1a,
    // stable across runs and platforms, unlike qHash). The map preview takes the same seed.
    [[nodiscard]] static qint64 seedFromText(const QString &text);

    const QString &root() const { return m_root; }
    // Newest first by last played time.
    QList<Entry> list() const;
    // Creates the directory and world.json for a new world; returns the new entry. The generator's
    // setup runs when the world first opens.
    [[nodiscard]] QcResult<Entry> create(const QString &name, qint64 seed, const NewWorld &setup = {}) const;
    // Deletes a world directory. Only plain folder names directly below the root are accepted.
    [[nodiscard]] QcStatus remove(const QString &folder) const;
    QString directoryOf(const QString &folder) const;

private:
    QString m_root;
};
