// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/controllers/QcHandbookController.h"

#include "app/controllers/QcSettingsController.h"
#include "app/controllers/QcWorldLibraryController.h"
#include "app/debug/QcAppLogging.h"
#include "app/models/QcItemIcons.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/icons/QcBlockIcon.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/textures/QcGpuLimits.h"
#include "clientcore/textures/QcTextureAtlas.h"
#include "common/handbook/builder/QcHandbookBuilder.h"
#include "worlds/QcHandbookRules.h"

#include <QElapsedTimer>

#include <memory>

QcHandbookController::QcHandbookController(const QcSessionParts &parts, const QcSettingsController &settings,
                                           const QcWorldLibraryController &library, QcItemIcons &icons, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
    , m_library(library)
    , m_icons(icons)
    , m_model(new QcHandbookModel(this))
    , m_navigation(new QcHandbookNavigation(*m_model, this))
{
}

void QcHandbookController::ensureHandbook()
{
    const QcKeyBindings &keys = m_settings.keys()->bindings();
    if (m_model->handbook() && m_generation == m_library.contentGeneration() && m_language == m_settings.contentLanguage() && m_keys == keys) {
        return;
    }

    QElapsedTimer timer;
    timer.start();
    const QcContentRegistry &content = m_library.content();
    const qint64 loadedMs = timer.elapsed();
    auto book = std::make_shared<const QcHandbook>(
        QcHandbookBuilder::build(content, m_settings.contentLanguage(), QcHandbookRules::values(content), keys.placeholderKeys()));
    const qint64 builtMs = timer.elapsed() - loadedMs;
    for (const QString &problem : book->problems()) {
        qCWarning(lcGame) << "Handbook:" << problem;
    }

    // In the menu nothing has drawn icons yet; draw them from the same content.
    if (!m_icons.isReady()) {
        // Icons only: tiles of at most 32 pixels keep this quick on the GUI thread whatever the
        // packs' resolution (decision E-300).
        QcTextureAtlas::Options options;
        options.maxExtent = QcGpuLimits::maxTextureSize();
        options.maxTileSize = QcBlockIcon::sourceTileSize;
        const QcTextureAtlas atlas = QcTextureAtlas::build(content, m_settings.texturePacks()->stack(), options);
        const QcBlockRenderTable table(content, atlas);
        m_icons.draw(content, atlas, &table);
    }
    qCInfo(lcGame).nospace() << "Handbook: " << book->entries().size() << " entries; content " << loadedMs << " ms, built in " << builtMs
                             << " ms, icons " << (timer.elapsed() - loadedMs - builtMs) << " ms";

    m_generation = m_library.contentGeneration();
    m_language = m_settings.contentLanguage();
    m_keys = keys;
    m_model->setHandbook(std::move(book));
}

void QcHandbookController::open(const QString &target)
{
    ensureHandbook();
    setTarget(m_model->resolve(target));
    m_query = this->target().isEmpty() ? target : QString();
    m_navigation->start(this->target(), m_query);
    setOpen(true);
    emit opening();
    logFirstFrame();
    emit openChanged();
}

void QcHandbookController::logFirstFrame()
{
    if (!m_parts.window) {
        return;
    }

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();
    auto connection = std::make_shared<QMetaObject::Connection>();
    // frameSwapped comes from the render thread; a second queued call may already wait.
    *connection = connect(m_parts.window, &QQuickWindow::frameSwapped, this, [timer, connection] {
        if (*connection) {
            qCInfo(lcGame) << "Handbook: first frame after opening" << timer->elapsed() << "ms";
            QObject::disconnect(*connection);
            *connection = {};
        }
    });
}

void QcHandbookController::close()
{
    if (!isOpen()) {
        return;
    }

    setOpen(false);
    emit openChanged();
}

void QcHandbookController::refreshIfOpen()
{
    if (isOpen()) {
        ensureHandbook();
    }
}
