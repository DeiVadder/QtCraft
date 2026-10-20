#include "app/controllers/QcHudState.h"

#include "app/controllers/QcSettingsController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/camera/QcLabelProjector.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"

using namespace Qt::StringLiterals;

QcHudState::QcHudState(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
    , m_labels(new QcWorldLabelModel(this))
    , m_debug(new QcDebugHud(parts, settings, this))
{
}

void QcHudState::updateTarget()
{
    const QcClientSession &session = *m_parts.session;
    const QcContentRegistry &content = *session.content();
    const std::optional<QcRaycastHit> &target = session.hand().target();
    const std::optional<quint32> targetEntity = session.hand().targetEntity();
    QString targetLabel;
    m_targetName.clear();
    if (targetEntity && session.entities().value(*targetEntity).isMinecart()) {
        const QcItemStack item = session.entities().value(*targetEntity).item;
        const QString key = content.isValidItem(item.item) ? content.item(item.item).translationKey : u"item.qtcraft.minecart"_s;
        targetLabel = content.translations().translate(key, m_settings.contentLanguage());
    } else if (targetEntity) {
        const QcClientEntity entity = session.entities().value(*targetEntity);
        const QcMobDefinition *creature = entity.creature(content);
        targetLabel = creature ? content.translations().translate(creature->translationKey, m_settings.contentLanguage()) : entity.name;
        if (creature) {
            m_targetName = u"mob:"_s + creature->name;
        }
        targetLabel += u" (%1)"_s.arg(qRound(entity.health));
    } else if (target) {
        if (const std::optional<QcBlockId> block = session.world()->blockAt(target->block)) {
            const QcBlockDefinition &definition = content.block(*block);
            targetLabel = content.translations().translate(definition.translationKey, m_settings.contentLanguage());
            m_targetName = definition.name;
        }
    }

    if (targetLabel != m_targetLabel || session.hand().miningProgress() != m_miningProgress) {
        m_targetLabel = targetLabel;
        m_miningProgress = session.hand().miningProgress();
        emit frameStateChanged();
    }
}

void QcHudState::updateTimeOfDay(double alpha)
{
    const double time = m_parts.session->worldMirror().worldTime() + alpha * QcPhysics::tickDuration;
    const QString label = QcDayCycle::isNight(time) ? tr("Night") : tr("Day");
    if (label != m_timeOfDay) {
        m_timeOfDay = label;
        emit frameStateChanged();
    }
}

void QcHudState::addChatLine(const QString &text)
{
    m_chatLines.append(text);
    // Only the last few lines stay on screen.
    constexpr qsizetype maxChatLines = 8;
    while (m_chatLines.size() > maxChatLines) {
        m_chatLines.removeFirst();
    }
    emit chatLinesChanged();
}

QList<QcWorldLabel> QcHudState::standLabels() const
{
    QList<QcWorldLabel> labels;
    for (const QcClientEntity &entity : m_parts.session->entities()) {
        if (entity.isArmourStand() && entity.stand.showName && !entity.name.isEmpty()) {
            const double top = QcStandPose::bodyHeight * (entity.stand.small ? 0.5 : 1.0) + 0.35;
            labels.append({entity.position + QcVec3{0.0, top, 0.0}, entity.name, {}, false});
        }
    }
    return labels;
}

const QHash<QcChunkPos, QList<QcWorldLabel>> &QcHudState::labelsToDraw()
{
    static const QHash<QcChunkPos, QList<QcWorldLabel>> none;
    const QHash<QcChunkPos, QList<QcWorldLabel>> &world = m_settings.showLabels() ? m_parts.session->worldMirror().extras().labels() : none;
    QList<QcWorldLabel> stands = standLabels();
    if (stands.isEmpty()) {
        return world;
    }

    // Merged again only when the session's labels (a shared copy is kept to notice) or the names change.
    if (!world.isSharedWith(m_mergedFrom) || stands != m_standLabels) {
        m_mergedFrom = world;
        m_mergedLabels = world;
        for (const QcWorldLabel &label : std::as_const(stands)) {
            const QcBlockPos at{int(std::floor(label.position.x)), 0, int(std::floor(label.position.z))};
            m_mergedLabels[at.chunk()].append(label);
        }
        m_standLabels = std::move(stands);
    }
    return m_mergedLabels;
}

void QcHudState::updateLabels()
{
    const QcClientSession &session = *m_parts.session;
    // World labels follow the setting; names of armour stands that show them are always drawn.
    const QHash<QcChunkPos, QList<QcWorldLabel>> &labels = labelsToDraw();
    if (labels.isEmpty() || !session.content()) {
        m_labels->clear();
        return;
    }

    const QcLabelProjector::View view{m_parts.view->camera(), m_parts.view->width(), m_parts.view->height()};
    m_labels->update(QcLabelProjector::visible(labels, view, session.world()), *session.content(), m_settings.contentLanguage());
}
