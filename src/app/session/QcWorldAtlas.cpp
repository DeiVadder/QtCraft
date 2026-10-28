#include "app/session/QcWorldAtlas.h"

#include "app/controllers/QcHeldMap.h"
#include "app/controllers/QcTexturePackController.h"
#include "app/debug/QcAppLogging.h"
#include "app/models/QcItemIcons.h"
#include "app/preview/QcPreviewSource.h"
#include "app/rendering/QcRenderBridge.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/meshing/QcBlockRenderTable.h"
#include "clientcore/meshing/QcMeshScheduler.h"
#include "clientcore/session/QcClientSession.h"
#include "clientcore/textures/QcTextureAtlas.h"

#include <QElapsedTimer>

using namespace Qt::StringLiterals;

QcWorldAtlas::QcWorldAtlas(QcSessionParts &parts, const Targets &targets)
    : m_parts(parts)
    , m_targets(targets)
{
}

QcWorldAtlas::~QcWorldAtlas() = default;

void QcWorldAtlas::enterWorld()
{
    const std::shared_ptr<const QcTextureAtlas> atlas = m_targets.texturePacks.enterWorld(m_parts.session->content());
    const QStringList problems = atlas->problems();
    for (const QString &problem : problems) {
        qCWarning(lcGame) << "Texture:" << problem;
    }

    setAtlas(nullptr);
    apply(atlas);
}

void QcWorldAtlas::apply(const std::shared_ptr<const QcTextureAtlas> &atlas, std::shared_ptr<const QcMaterialAtlas> materials)
{
    if (!m_parts.session || !m_parts.session->content() || !atlas) {
        return;
    }
    if (atlas == this->atlas()) {
        if (m_parts.view) {
            m_parts.view->setMaterials(std::move(materials));
        }
        return;
    }

    // The time on the GUI thread is logged for the pack switch benchmark (decision E-332); the
    // upload follows on the render thread.
    QElapsedTimer timer;
    timer.start();
    const QcContentRegistry &content = *m_parts.session->content();
    // The UVs depend only on the list of textures (E-300): a new pack only needs the new image, the
    // meshes stay; a different layout (other content) needs a new render table and new meshes.
    if (!this->atlas() || !m_renderTable || !atlas->sameLayout(*this->atlas())) {
        m_renderTable = std::make_shared<const QcBlockRenderTable>(content, *atlas);
        m_parts.meshes->setRenderTable(m_renderTable);
    }
    setAtlas(atlas);
    m_targets.render.setAtlas(atlas, m_renderTable);
    m_targets.preview.useWorld(m_parts.session->content(), atlas);
    const qint64 iconsStart = timer.nsecsElapsed();
    m_targets.icons.draw(content, *atlas, m_renderTable.get());
    m_targets.heldMap.useAtlas(content, *atlas);
    const qint64 iconsNs = timer.nsecsElapsed() - iconsStart;
    if (m_parts.view) {
        m_parts.view->setAtlas(atlas->levels(), atlas->animationMap(), atlas->animationCycleTicks());
        m_parts.view->setMaterials(std::move(materials));
    }
    const qint64 totalNs = timer.nsecsElapsed();
    qCInfo(lcGame).noquote() << u"Texture atlas applied in %1 ms (item icons %2 ms)"_s.arg(static_cast<double>(totalNs) / 1e6, 0, 'f', 1)
                                    .arg(static_cast<double>(iconsNs) / 1e6, 0, 'f', 1);
}

void QcWorldAtlas::leaveWorld()
{
    m_targets.texturePacks.leaveWorld();
    setAtlas(nullptr);
    m_renderTable.reset();
    m_targets.render.setAtlas(nullptr, nullptr);
}
