// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "worlds/QcModStatus.h"

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class QcContentRegistry;

// The saved worlds and the mods for the menu (GameController.library), and the content they make up:
// base game plus enabled mods, loaded on demand while no world runs.
class QcWorldLibraryController : public QObject
{
    Q_OBJECT
    // WorldLibrary in QML, for its enumeration (WorldLibrary.WorldType.Flat).
    QML_NAMED_ELEMENT(WorldLibrary)
    QML_UNCREATABLE("Provided by GameController.library")
    Q_PROPERTY(QVariantList worlds READ worlds NOTIFY worldsChanged FINAL)
    // The world "Continue" opens: the one played last that can be opened (its error empty); null
    // when there is none.
    Q_PROPERTY(QVariant continueWorld READ continueWorld NOTIFY worldsChanged FINAL)
    // The worlds of the list below the card: all but continueWorld, the last played first.
    Q_PROPERTY(QVariantList otherWorlds READ otherWorlds NOTIFY worldsChanged FINAL)
    Q_PROPERTY(QVariantList mods READ mods NOTIFY modsChanged FINAL)
    Q_PROPERTY(QString modsDirectory READ modsDirectory NOTIFY modsChanged FINAL)

public:
    // The world types the menu offers, each a world generator (QcWorldGenerators): a terrain
    // world, a flat one, the void and the showcase.
    enum class WorldType {
        Terrain,
        Flat,
        Void,
        Showcase
    };
    Q_ENUM(WorldType)

    explicit QcWorldLibraryController(QObject *parent = nullptr);
    ~QcWorldLibraryController() override;
    Q_DISABLE_COPY_MOVE(QcWorldLibraryController)

    QVariantList worlds() const { return m_worlds; }
    QVariant continueWorld() const;
    QVariantList otherWorlds() const;
    QVariantList mods() const { return m_mods; }
    QString modsDirectory() const { return m_modsDirectory; }
    // Where mods are looked for; empty loads only the base game (test hook --no-mods).
    void setModsDirectory(const QString &directory);

    // The saved worlds, the last played first: folder, name, seed, worldType (WorldType), typeBlock
    // (the block whose icon stands for the type), gameMode (the mode of new players, GameMode.Mode),
    // lastPlayed (when, as the menu shows it) and error (why it cannot be opened, else empty).
    Q_INVOKABLE void refreshWorlds();
    // The generator name of `type` ("terrain", "flat", "void", "showcase"), as world.json keeps it.
    [[nodiscard]] static QLatin1StringView generatorOf(WorldType type);
    // The world type of the generator name `generator`; a type the menu does not know counts as a
    // terrain world, as worlds without a type do.
    [[nodiscard]] static WorldType worldTypeOf(const QString &generator);
    // When a world was last played, as the menu shows it at `now`: "Today, 21:14", "Yesterday,
    // 08:05", else the short date in the user's locale.
    [[nodiscard]] static QString playedText(const QDateTime &played, const QDateTime &now);
    // A world that failed to open for a reason the list cannot see (a damaged per-region file):
    // its entry shows `message` until the game restarts or the world opens (an empty message).
    void noteUnplayable(const QString &folder, const QString &message);
    // Deletes a saved world permanently; the UI asks for confirmation first.
    Q_INVOKABLE void deleteWorld(const QString &folder);
    // Shows a saved world's folder in the file manager (the world row's "…" menu).
    Q_INVOKABLE void openWorldFolder(const QString &folder) const;
    // Mod list as found on disk: id, name, version, description, state (ModStatus.State: Loaded,
    // Disabled or Failed, also for a mod whose content the last world refused), reason.
    Q_INVOKABLE void refreshMods();
    // Takes effect when the next world starts.
    Q_INVOKABLE void setModEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void openModsFolder() const;

    // The mods a world just loaded: failures are shown in the mod list, the loaded ones logged.
    void recordLoadedMods(const QList<QcModStatus> &mods);
    // Base game plus enabled mods with the server-side data (world generation, chapters): the content
    // of the running world or, in the menu, loaded on demand.
    const QcContentRegistry &content() const;
    // The content loaded so far; null when none is.
    std::shared_ptr<const QcContentRegistry> loadedContent() const { return m_content; }
    // The content a world was just started with.
    void useContent(std::shared_ptr<const QcContentRegistry> content);
    // Bumped whenever content() may change (mods switched, world started).
    int contentGeneration() const { return m_contentGeneration; }

signals:
    void worldsChanged();
    void modsChanged();
    // The loaded content was dropped: mods were switched or looked for elsewhere.
    void contentReset();

private:
    void resetContent();

    QVariantList m_worlds;
    QVariantList m_mods;
    // Mods that passed discovery but failed when the last world loaded its content, with the reason.
    QHash<QString, QString> m_modLoadFailures;
    // Worlds that failed to open, with the reason (noteUnplayable).
    QHash<QString, QString> m_unplayable;
    QString m_modsDirectory;
    mutable std::shared_ptr<const QcContentRegistry> m_content;
    int m_contentGeneration = 0;
};
