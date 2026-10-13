#pragma once

#include "QcTexturePackLibrary.h"
#include "QcTextureStackBuild.h"

#include <QFutureWatcher>
#include <QObject>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>
#include <memory>

class QcContentRegistry;
class QcTextureAtlas;
class QcTexturePackPreviewProvider;

// The texture packs of the settings (GameController.settings.texturePacks, decisions E-297 and
// E-301): which packs exist, which are active in which order, their problems, previews and the
// state of the atlas. Listing, loading and building run in a worker thread (QcTextureStackBuild)
// while the game keeps drawing with the previous atlas; atlasBuilt() hands a new atlas over. Every
// change raises a generation, and a result of an older generation is dropped when it arrives, so a
// slow build never undoes a later change. Lives in the GUI thread.
class QcTexturePackController : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // Every pack found, the active ones first in priority order: maps with id, name, description,
    // resolution, license, credits, origin, active, error, problems and preview (an image URL or "").
    Q_PROPERTY(QVariantList packs READ packs NOTIFY packsChanged FINAL)
    // What the current atlas uses (resolution, size, memory, build time), "" before the first one.
    Q_PROPERTY(QString status READ status NOTIFY statusChanged FINAL)
    // Messages about the whole selection: packs that are gone, a lowered resolution.
    Q_PROPERTY(QStringList notices READ notices NOTIFY statusChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged FINAL)
    Q_PROPERTY(QString userDirectory READ userDirectory CONSTANT FINAL)

public:
    // `modsDirectory` gives the folder of the installed mods ("" for none) when packs are listed.
    explicit QcTexturePackController(std::function<QString()> modsDirectory, QObject *parent = nullptr);
    ~QcTexturePackController() override;

    void setPreviewProvider(QcTexturePackPreviewProvider *provider) { m_previewProvider = provider; }

    // QTCRAFT_TEXTURE_PACKS (call before loadSelection): used for this run instead of the stored
    // selection, and not stored unless the player changes the packs.
    void overrideSelection(const QStringList &ids);
    // The stored selection, read with the settings: lists and loads the packs in the background.
    void loadSelection(const QStringList &ids);
    // What the settings store.
    QStringList storedSelection() const { return m_overridden ? m_stored : m_active; }
    // The active pack ids, the highest priority first (stored or for this run).
    const QStringList &activeSelection() const { return m_active; }
    // For this run only (benchmark pack switch, decision E-332): makes `ids` the whole active
    // selection at once, one build for it, and stores nothing: storedSelection() keeps what the
    // settings had and selectionChanged() is not emitted, so the settings are not saved over.
    void selectForRun(const QStringList &ids);

    // The loaded active packs, the highest priority first.
    const QcTexturePackStack &stack() const { return m_stack; }
    // A world's content arrived: its atlas, built now with the active packs (a build still running
    // is not waited for, its result is dropped); later changes rebuild it in the background.
    std::shared_ptr<const QcTextureAtlas> enterWorld(std::shared_ptr<const QcContentRegistry> content);
    // The world ended: its content is let go and no atlas of it is handed out any more.
    void leaveWorld();

    QVariantList packs() const;
    QString status() const { return m_status; }
    QStringList notices() const { return m_notices; }
    bool busy() const { return m_atlasBuild.isRunning(); }
    QString userDirectory() const;
    // Changes with every change of the selection or content (tests).
    quint64 generation() const { return m_generation; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setActive(const QString &id, bool active);
    Q_INVOKABLE void move(const QString &id, int steps);
    Q_INVOKABLE void openFolder() const;

signals:
    void packsChanged();
    void statusChanged();
    // The player changed the selection (the settings store it).
    void selectionChanged();
    // The packs of the new selection are loaded (the menu redraws its icons and previews).
    void stackChanged();
    // A new atlas of the world's content for the current selection.
    void atlasBuilt(std::shared_ptr<const QcTextureAtlas> atlas);

private:
    void select(const QStringList &ids);
    // Makes `ids` (cleaned) the active packs and starts their build; false when nothing changed.
    bool activate(const QStringList &ids);
    void startBuild();
    void onBuilt();
    void apply(const QcTextureStackBuild::Result &result);
    void updatePreviews();
    void describe(const QcTextureAtlas &atlas, qint64 milliseconds);
    QcTextureStackBuild::Request request() const;

    std::function<QString()> m_modsDirectory;
    QcTexturePackPreviewProvider *m_previewProvider = nullptr;
    QList<QcTexturePackLibrary::Entry> m_entries;
    QStringList m_active;
    QStringList m_stored;
    bool m_overridden = false;
    // The packs must be listed again (Reload, start) before the next result counts as current.
    bool m_rescan = true;
    QHash<QString, QcTexturePackRef> m_loaded;
    QHash<QString, QcTextureStackBuild::Preview> m_previews;
    QcTexturePackStack m_stack;
    std::shared_ptr<const QcContentRegistry> m_content;
    QFutureWatcher<QcTextureStackBuild::Result> m_atlasBuild;
    quint64 m_generation = 1;
    // A change arrived while a build was running: build again when it ends.
    bool m_buildAgain = false;
    QString m_status;
    QStringList m_notices;
    QStringList m_selectionNotices;
    int m_previewGeneration = 0;
};
