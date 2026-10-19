#pragma once

#include "worlds/QcModStatus.h"

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
    QML_ANONYMOUS
    Q_PROPERTY(QVariantList worlds READ worlds NOTIFY worldsChanged FINAL)
    Q_PROPERTY(QVariantList mods READ mods NOTIFY modsChanged FINAL)
    Q_PROPERTY(QString modsDirectory READ modsDirectory NOTIFY modsChanged FINAL)

public:
    explicit QcWorldLibraryController(QObject *parent = nullptr);
    ~QcWorldLibraryController() override;

    QVariantList worlds() const { return m_worlds; }
    QVariantList mods() const { return m_mods; }
    QString modsDirectory() const { return m_modsDirectory; }
    // Where mods are looked for; empty loads only the base game (test hook --no-mods).
    void setModsDirectory(const QString &directory);

    Q_INVOKABLE void refreshWorlds();
    // A world that failed to open for a reason the list cannot see (a damaged per-region file):
    // its entry shows `message` until the game restarts or the world opens (an empty message).
    void noteUnplayable(const QString &folder, const QString &message);
    // Deletes a saved world permanently; the UI asks for confirmation first.
    Q_INVOKABLE void deleteWorld(const QString &folder);
    // Mod list as found on disk: id, name, version, description, state (loaded/disabled/failed), reason.
    Q_INVOKABLE void refreshMods();
    // Takes effect when the next world starts.
    Q_INVOKABLE void setModEnabled(const QString &id, bool enabled);
    Q_INVOKABLE void openModsFolder();

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
