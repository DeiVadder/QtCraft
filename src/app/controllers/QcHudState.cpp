#include "app/controllers/QcHudState.h"

#include "app/controllers/QcSettingsController.h"
#include "app/session/QcSessionParts.h"
#include "clientcore/camera/QcLabelProjector.h"
#include "clientcore/player/QcDials.h"
#include "clientcore/session/QcClientEntity.h"
#include "clientcore/session/QcClientSession.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcDayCycle.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace {

// The name of `stack` in `language`: its custom name, else its item's; empty for an unknown item.
QString stackName(const QcContentRegistry &content, const QcItemStack &stack, const QString &language)
{
    if (!stack.name().isEmpty()) {
        return stack.name();
    }
    return content.isValidItem(stack.item) ? content.translations().translate(content.item(stack.item).translationKey, language) : QString();
}

// The translated name of the item `item`, else of the item with the content key `fallback`.
QString itemName(const QcContentRegistry &content, QcItemId item, const QString &fallback, const QString &language)
{
    const QString key = content.isValidItem(item) ? content.item(item).translationKey : fallback;
    return content.translations().translate(key, language);
}

} // namespace

QcHudState::QcHudState(const QcSessionParts &parts, const QcSettingsController &settings, QObject *parent)
    : QObject(parent)
    , m_parts(parts)
    , m_settings(settings)
    , m_heldMap(new QcHeldMap(parts, this))
    , m_labels(new QcWorldLabelModel(this))
    , m_debug(new QcDebugHud(parts, settings, this))
{
}

QcHudState::EntityTarget QcHudState::entityTarget(const QcContentRegistry &content, const QcClientEntity &entity, const QString &language)
{
    if (entity.isMinecart()) {
        return {itemName(content, entity.item.item, u"item.qtcraft.minecart"_s, language), {}};
    }
    if (entity.isHanging()) {
        const bool painting = entity.hanging.type == QcHangingType::Painting;
        const QString own = itemName(content, entity.held, painting ? u"item.qtcraft.painting"_s : u"item.qtcraft.item_frame"_s, language);
        const QString shown = entity.item.isEmpty() ? QString() : stackName(content, entity.item, language);
        //: An item frame in the target text: %1 the frame, %2 the item it holds.
        return {shown.isEmpty() ? own : tr("%1 (%2)", "item frame with its item").arg(own, shown), {}};
    }
    if (entity.isArmourStand()) {
        return {entity.name.isEmpty() ? content.translations().translate(u"item.qtcraft.armor_stand"_s, language) : entity.name, {}};
    }

    const QcMobDefinition *creature = entity.creature(content);
    if (!creature && !entity.isPlayer()) {
        return {entity.name, {}};
    }
    QString label = creature ? content.translations().translate(creature->translationKey, language) : entity.name;
    // A creature a name tag named (decision E-866) shows its name first.
    if (creature && !entity.name.isEmpty()) {
        //: A named creature in the target text: %1 its name, %2 its kind.
        label = tr("%1 (%2)", "named creature").arg(entity.name, label);
    }
    label += u" (%1)"_s.arg(qRound(entity.health));
    return {label, creature ? u"mob:"_s + creature->name : QString()};
}

void QcHudState::updateTarget()
{
    const QcClientSession &session = *m_parts.session;
    const QcContentRegistry &content = *session.content();
    const std::optional<QcRaycastHit> &target = session.hand().target();
    const std::optional<quint32> targetEntity = session.hand().targetEntity();
    QString targetLabel;
    m_targetName.clear();
    if (targetEntity) {
        const EntityTarget entity = entityTarget(content, session.entities().value(*targetEntity), m_settings.contentLanguage());
        targetLabel = entity.label;
        m_targetName = entity.name;
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

void QcHudState::updateDial(double alpha)
{
    const QcClientSession &session = *m_parts.session;
    const QcContentRegistry *content = session.content().get();
    const QcItemStack held = session.inventory().slot(session.inventory().selectedHotbarSlot());
    const QcItemDial kind = content && content->isValidItem(held.item) ? content->item(held.item).dial : QcItemDial::None;
    const double time = session.worldMirror().worldTime() + alpha * QcPhysics::tickDuration;

    Dial dial = Dial::None;
    double angle = 0.0;
    QString clock;
    if (kind == QcItemDial::Compass) {
        dial = Dial::Compass;
        const QcMsg::Welcome &welcome = session.welcome();
        angle = QcDials::compassAngle(session.playerMotion().position, session.intent().yaw, welcome.spawnX, welcome.spawnZ);
    } else if (kind == QcItemDial::Clock) {
        dial = Dial::Clock;
        angle = QcDials::clockAngle(time);
        const QcDials::ClockTime shown = QcDials::clockTime(time);
        clock = u"%1:%2"_s.arg(shown.hours).arg(shown.minutes, 2, 10, QChar(u'0'));
    }

    // Small turns are not worth a repaint of the HUD.
    if (dial != m_dial || std::abs(angle - m_dialAngle) >= 0.5 || clock != m_clockText) {
        m_dial = dial;
        m_dialAngle = angle;
        m_clockText = clock;
        emit dialChanged();
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
        // Creatures a name tag named (decision E-866) carry their name above the head.
        const QcMobDefinition *creature = m_parts.session->content() ? entity.creature(*m_parts.session->content()) : nullptr;
        if (creature && !entity.name.isEmpty()) {
            const double scale = (entity.flags & QcMsg::Young) != 0 ? QcMsg::youngScale : 1.0;
            labels.append({entity.position + QcVec3{0.0, creature->height * scale + 0.35, 0.0}, entity.name, {}, false});
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
            const QcBlockPos at{static_cast<int>(std::floor(label.position.x)), 0, static_cast<int>(std::floor(label.position.z))};
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
