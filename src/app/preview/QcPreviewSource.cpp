#include "app/preview/QcPreviewSource.h"

#include "app/controllers/QcCharacterSettings.h"
#include "app/controllers/QcTexturePackController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/textures/QcGpuLimits.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/content/registry/QcContentRegistry.h"
#include "common/core/QcWorkerPool.h"

#include <QLoggingCategory>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcPreview, "qtcraft.preview")

} // namespace

QcPreviewSource::QcPreviewSource(const QcSessionParts &parts, const QcWorldLibraryController &library, const QcCharacterSettings &character,
                                 const QcTexturePackController &texturePacks, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_library(library)
    , m_character(character)
    , m_texturePacks(texturePacks)
{
}

// m_atlasWorker is destroyed first (declared last): it joins a running build, whose last access to
// the mailbox and to this object (posting the queued call) happens before the join.
QcPreviewSource::~QcPreviewSource() = default;

void QcPreviewSource::useWorld(std::shared_ptr<const QcContentRegistry> content, std::shared_ptr<const QcTextureAtlas> atlas)
{
    m_content = std::move(content);
    m_atlas = std::move(atlas);
    m_inWorld = true;
    // An atlas still being built for the menu is dropped when it arrives.
    m_menuGeneration = -1;
    m_pendingContent.reset();
    ++m_version;
}

void QcPreviewSource::useMenu()
{
    m_content.reset();
    m_atlas.reset();
    m_inWorld = false;
    m_menuGeneration = -1;
    m_pendingContent.reset();
    ++m_version;
}

void QcPreviewSource::ensureMenuAtlas()
{
    if (m_inWorld || m_menuGeneration == m_library.contentGeneration()) {
        return;
    }

    // The library loads the menu content on demand; it stays loaded for the handbook and sounds.
    m_library.content();
    m_menuGeneration = m_library.contentGeneration();
    m_pendingContent = m_library.loadedContent();
    if (!m_pendingContent) {
        return;
    }

    // The atlas only reads the content and the loaded texture packs, which nobody changes (shared
    // and const).
    m_atlasTimer.start();
    QcTextureAtlas::Options options;
    options.maxExtent = QcGpuLimits::maxTextureSize();
    if (!m_atlasWorker) {
        m_atlasWorker = std::make_unique<QcWorkerPool>(1);
    }
    // A build for older content that has not started yet is not needed any more.
    m_atlasWorker->discardQueued();
    const quint64 build = ++m_atlasBuild;
    m_atlasWorker->submit([this, build, content = m_pendingContent, packs = m_texturePacks.stack(), options] {
        auto atlas = std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(*content, packs, options));
        if (m_builtAtlases.push({build, std::move(atlas)})) {
            QMetaObject::invokeMethod(this, &QcPreviewSource::onMenuAtlasBuilt, Qt::QueuedConnection);
        }
    });
}

void QcPreviewSource::onMenuAtlasBuilt()
{
    std::shared_ptr<const QcTextureAtlas> latest;
    for (BuiltAtlas &built : m_builtAtlases.takeAll()) {
        if (built.build == m_atlasBuild) {
            latest = std::move(built.atlas);
        }
    }

    // Built for content that is no longer shown (a world started, mods switched, a newer build
    // started): dropped.
    if (!latest || m_inWorld || !m_pendingContent || m_menuGeneration != m_library.contentGeneration()) {
        return;
    }

    m_content = std::exchange(m_pendingContent, nullptr);
    m_atlas = std::move(latest);
    ++m_version;
    qCInfo(lcPreview) << "Character preview: menu atlas ready" << m_atlasTimer.elapsed() << "ms after the request (built in the background)";
}

std::shared_ptr<const QcContentRegistry> QcPreviewSource::content()
{
    ensureMenuAtlas();
    return m_content;
}

std::shared_ptr<const QcTextureAtlas> QcPreviewSource::atlas()
{
    ensureMenuAtlas();
    return m_atlas;
}

const QcAppearance &QcPreviewSource::appearance() const
{
    return m_character.appearance();
}

QcEquipment QcPreviewSource::equipment() const
{
    return m_parts.session ? m_parts.session->equipment() : QcEquipment();
}

QcItemId QcPreviewSource::heldItem(bool mining)
{
    if (m_parts.session) {
        const QcItemStack &selected = m_parts.session->inventory().selectedStack();
        return selected.isEmpty() ? qcNoItem : selected.item;
    }
    if (!mining || !content()) {
        return qcNoItem;
    }
    return m_content->itemId(u"qtcraft:iron_pickaxe"_s).value_or(qcNoItem);
}
