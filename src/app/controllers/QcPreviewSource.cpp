#include "QcPreviewSource.h"

#include "QcCharacterSettings.h"
#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcSessionParts.h"
#include "QcTextureAtlas.h"
#include "QcWorldLibraryController.h"

#include <QLoggingCategory>
#include <QtConcurrent/QtConcurrentRun>

using namespace Qt::StringLiterals;

namespace {

Q_LOGGING_CATEGORY(lcPreview, "qtcraft.preview")

} // namespace

QcPreviewSource::QcPreviewSource(const QcSessionParts &parts, const QcWorldLibraryController &library, const QcCharacterSettings &character,
                                 QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_library(library)
    , m_character(character)
{
    connect(&m_atlasBuild, &AtlasWatcher::finished, this, &QcPreviewSource::onMenuAtlasBuilt);
}

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

    // The atlas only reads the content, which nobody changes (shared and const).
    m_atlasTimer.start();
    m_atlasBuild.setFuture(QtConcurrent::run([content = m_pendingContent] {
        return std::make_shared<const QcTextureAtlas>(QcTextureAtlas::build(*content));
    }));
}

void QcPreviewSource::onMenuAtlasBuilt()
{
    // Built for content that is no longer shown (a world started, mods switched): dropped.
    if (m_inWorld || !m_pendingContent || m_menuGeneration != m_library.contentGeneration()) {
        return;
    }

    m_content = std::exchange(m_pendingContent, nullptr);
    m_atlas = m_atlasBuild.result();
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
