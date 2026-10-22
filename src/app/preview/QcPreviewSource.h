#pragma once

#include "clientcore/preview/QcPreviewHook.h"
#include "common/content/definitions/QcAppearance.h"
#include "common/content/definitions/QcItemDefinition.h"
#include "common/core/QcMailbox.h"
#include "common/gameplay/QcEquipment.h"

#include <QElapsedTimer>
#include <QObject>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <utility>

class QcCharacterSettings;
class QcContentRegistry;
class QcTexturePackController;
class QcTextureAtlas;
class QcWorkerPool;
class QcWorldLibraryController;
struct QcSessionParts;

// What the character previews (CharacterPreview items) draw the player with
// (GameController.preview): the content and texture atlas of the running world, or in the menu the
// base game plus enabled mods with an atlas built in the background on first use, the player's
// look, the held item and the --preview test hook. Also sums up the GUI time the previews spend
// building their meshes, for the debug display (decision E-231). Lives in the GUI thread.
class QcPreviewSource : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS
    // The window's frame rate limit in force (decision E-835), 0 for none: previews draw no more
    // often than the window.
    Q_PROPERTY(int frameLimit READ frameLimit NOTIFY frameLimitChanged FINAL)

public:
    QcPreviewSource(const QcSessionParts &parts, const QcWorldLibraryController &library, const QcCharacterSettings &character,
                    const QcTexturePackController &texturePacks, QObject *parent = nullptr);
    ~QcPreviewSource() override;

    // The running world's content and atlas (QcGameController once the content arrived).
    void useWorld(std::shared_ptr<const QcContentRegistry> content, std::shared_ptr<const QcTextureAtlas> atlas);
    // Back in the menu, or the menu content changed (mods switched): drawn from the menu content.
    void useMenu();

    // Null while nothing can be drawn. In the menu the first call starts building the atlas in the
    // background (the content itself is loaded here, the library is not thread-safe); both stay
    // null until it is ready, so the first frames of the menu do not wait for it.
    std::shared_ptr<const QcContentRegistry> content();
    std::shared_ptr<const QcTextureAtlas> atlas();
    // Changes whenever content() or atlas() may have changed.
    quint64 version() const { return m_version; }

    const QcAppearance &appearance() const;
    // The item in the player's hand: the selected hotbar stack in a world; in the menu a pickaxe
    // while mining, so the swing reads as mining.
    QcItemId heldItem(bool mining);
    // What the player wears in a world; nothing in the menu.
    QcEquipment equipment() const;

    void setHook(const QcPreviewHook &hook)
    {
        m_hook = hook;
        ++m_hookVersion;
    }
    const QcPreviewHook &hook() const { return m_hook; }
    // Changes with every setHook(); previews apply a new hook before their next frame.
    int hookVersion() const { return m_hookVersion; }

    int frameLimit() const { return m_frameLimit; }
    void setFrameLimit(int framesPerSecond)
    {
        if (framesPerSecond != m_frameLimit) {
            m_frameLimit = framesPerSecond;
            emit frameLimitChanged();
        }
    }

    // Offscreen runs (E-229): their window is never shown or active, its previews draw anyway.
    void setOffscreen(bool offscreen) { m_offscreen = offscreen; }
    bool offscreen() const { return m_offscreen; }
    // Benchmark hook QTCRAFT_PREVIEW_ACTIVE=1: previews in an inactive window animate as in an
    // active one (QcPreviewActivity::State::forceActive).
    void setForceActive(bool forceActive) { m_forceActive = forceActive; }
    bool forceActive() const { return m_forceActive; }

    // Mesh building time of the previews since the last takeBuildNs() (debug display).
    void addBuildNs(qint64 nanoseconds) { m_buildNs += nanoseconds; }
    qint64 takeBuildNs() { return std::exchange(m_buildNs, 0); }

signals:
    void frameLimitChanged();

private:
    // A menu atlas from the worker, tagged with the build it belongs to.
    struct BuiltAtlas
    {
        quint64 build = 0;
        std::shared_ptr<const QcTextureAtlas> atlas;
    };

    void ensureMenuAtlas();
    void onMenuAtlasBuilt();

    const QcSessionParts &m_parts;
    const QcWorldLibraryController &m_library;
    const QcCharacterSettings &m_character;
    const QcTexturePackController &m_texturePacks;
    std::shared_ptr<const QcContentRegistry> m_content;
    std::shared_ptr<const QcTextureAtlas> m_atlas;
    // Library content generation the menu atlas was built (or is being built) for; -1 for none
    // (world or not started).
    int m_menuGeneration = -1;
    // The content of the atlas being built, and how long that takes.
    std::shared_ptr<const QcContentRegistry> m_pendingContent;
    // Number of the latest atlas build; results of older builds are dropped.
    quint64 m_atlasBuild = 0;
    // Finished builds; the worker hands them over under a std::mutex and only wakes this object
    // with a queued call (ThreadSanitizer cannot see the synchronisation inside QtCore, E-231).
    QcMailbox<BuiltAtlas> m_builtAtlases;
    QElapsedTimer m_atlasTimer;
    bool m_offscreen = false;
    int m_frameLimit = 0;
    bool m_forceActive = false;
    bool m_inWorld = false;
    quint64 m_version = 1;
    QcPreviewHook m_hook;
    int m_hookVersion = 0;
    qint64 m_buildNs = 0;
    // One worker thread, started with the first menu atlas. Declared last: destroyed first, it
    // drops queued builds and joins a running one before the mailbox and this object go away.
    std::unique_ptr<QcWorkerPool> m_atlasWorker;
};
