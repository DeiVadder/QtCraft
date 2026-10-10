#pragma once

#include "QcResult.h"
#include "QcWorldStorage.h"

#include <QList>
#include <QString>

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
    };

    explicit QcWorldLibrary(QString root);

    // Base directory for worlds and mods. QTCRAFT_DATA_DIR overrides the platform's application data
    // location (tests, screenshots).
    static QString dataRoot();
    // <data root>/worlds
    static QString defaultRoot();

    const QString &root() const { return m_root; }
    // Newest first by last played time.
    QList<Entry> list() const;
    // Creates the directory and world.json for a world of the generator type `generator`
    // (QcWorldGenerators); returns the new entry. The generator's setup runs when the world first opens.
    // `options` are those of the generator (the layers of a flat world).
    QcResult<Entry> create(const QString &name, qint64 seed, bool keepInventory = true,
                           const QString &generator = QString::fromLatin1(QcWorldInfo::defaultGenerator), const QString &options = {}) const;
    // Deletes a world directory. Only plain folder names directly below the root are accepted.
    QcStatus remove(const QString &folder) const;
    QString directoryOf(const QString &folder) const;

private:
    QString m_root;
};
