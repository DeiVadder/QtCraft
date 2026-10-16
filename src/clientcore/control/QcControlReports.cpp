#include "QcControlReports.h"

#include "QcClientSession.h"
#include "QcContentRegistry.h"
#include "QcControlArguments.h"
#include "QcDayCycle.h"
#include "QcPhysics.h"
#include "QcSoundSpatial.h"

#include <QMetaEnum>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

QcControlReports::QcControlReports(const QcClientSession &session, const QStringList &chatLog)
    : m_session(session)
    , m_chatLog(chatLog)
{
}

QJsonArray QcControlReports::vectorJson(const QcVec3 &v)
{
    return {v.x, v.y, v.z};
}

QJsonObject QcControlReports::appearanceJson(const QcAppearance &look)
{
    const auto colour = [](quint32 rgb) -> QString { return u"#"_s + QString::number(rgb & 0xffffffu, 16).rightJustified(6, u'0'); };
    return {{u"outfit"_s, look.outfit},
            {u"hair"_s, look.hair},
            {u"cape"_s, look.cape},
            {u"skinColor"_s, colour(look.skinColor)},
            {u"hairColor"_s, colour(look.hairColor)},
            {u"shirtColor"_s, colour(look.shirtColor)},
            {u"pantsColor"_s, colour(look.pantsColor)}};
}

QJsonObject QcControlReports::playerStatus() const
{
    QJsonObject status;
    const QMetaEnum stateEnum = QMetaEnum::fromType<QcClientSession::State>();
    status.insert(u"state"_s, QString::fromLatin1(stateEnum.valueToKey(int(m_session.state()))));
    status.insert(u"tick"_s, qint64(m_session.tickCount()));
    status.insert(u"world"_s, m_session.worldName());
    status.insert(u"seed"_s, QString::number(m_session.seed()));

    const QcEntityMotion &motion = m_session.playerMotion();
    status.insert(u"position"_s, vectorJson(motion.position));
    status.insert(u"velocity"_s, vectorJson(motion.velocity));
    status.insert(u"onGround"_s, motion.onGround);
    status.insert(u"inWater"_s, motion.inWater);
    status.insert(u"flying"_s, motion.flying);
    // The minecart ridden (decision E-382) and the largest prediction correction since the session began.
    status.insert(u"riding"_s, m_session.riding().isRiding() ? QJsonValue(qint64(m_session.riding().vehicle())) : QJsonValue());
    status.insert(u"largestCorrection"_s, m_session.largestCorrection());
    status.insert(u"gameMode"_s, QString(QcGameModes::name(m_session.gameMode().mode())));
    status.insert(u"yaw"_s, m_session.intent().yaw);
    status.insert(u"pitch"_s, m_session.intent().pitch);
    status.insert(u"loadedChunks"_s, m_session.world() ? qint64(m_session.world()->chunkCount()) : 0);

    if (m_session.state() == QcClientSession::State::Disconnected) {
        status.insert(u"disconnectReason"_s, m_session.disconnectReason());
    }

    insertTarget(status);

    status.insert(u"health"_s, m_session.vitals().health());
    status.insert(u"maxHealth"_s, m_session.vitals().maxHealth());
    status.insert(u"burning"_s, m_session.vitals().burning());
    status.insert(u"dead"_s, m_session.isDead());
    status.insert(u"food"_s, m_session.vitals().food());
    status.insert(u"maxFood"_s, m_session.vitals().maxFood());
    status.insert(u"experienceLevel"_s, m_session.vitals().experienceLevel());
    status.insert(u"experiencePoints"_s, m_session.vitals().experiencePoints());
    status.insert(u"experienceForNext"_s, m_session.vitals().experienceForNext());
    status.insert(u"worldTime"_s, m_session.worldTime());
    status.insert(u"night"_s, QcDayCycle::isNight(m_session.worldTime()));
    status.insert(u"weather"_s, weatherJson());

    if (const std::optional<quint32> entity = m_session.targetEntity()) {
        status.insert(u"targetEntity"_s, qint64(*entity));
    }

    const QcItemStack held = m_session.inventory().selectedStack();
    status.insert(u"selectedSlot"_s, m_session.inventory().selectedHotbarSlot());
    status.insert(u"held"_s, stackJson(held));

    if (!m_chatLog.isEmpty()) {
        status.insert(u"messages"_s, QJsonArray::fromStringList(m_chatLog));
    }

    return status;
}

QJsonObject QcControlReports::weatherJson() const
{
    const QcClientWeather &weather = m_session.weather();
    const int eyeY = int(std::floor(m_session.playerMotion().position.y + QcPhysics::playerEyeHeight));
    const QcPrecipitation falling = weather.precipitationAt(eyeY);
    QString precipitation = u"none"_s;
    if (falling != QcPrecipitation::None) {
        precipitation = falling == QcPrecipitation::Snow ? u"snow"_s : u"rain"_s;
    }
    return {{u"rain"_s, weather.rain()},
            {u"thunder"_s, weather.thunder()},
            {u"precipitation"_s, precipitation},
            {u"dry"_s, weather.climate().dry},
            {u"snowFrom"_s, weather.climate().snowFrom},
            {u"strikes"_s, qint64(weather.strikes())}};
}

void QcControlReports::insertTarget(QJsonObject &status) const
{
    if (const auto &target = m_session.target()) {
        const std::optional<QcBlockId> block = m_session.world() ? m_session.world()->blockAt(target->block) : std::nullopt;
        status.insert(u"target"_s, QJsonObject{{u"x"_s, target->block.x},
                                               {u"y"_s, target->block.y},
                                               {u"z"_s, target->block.z},
                                               {u"face"_s, QcControlArguments::faceName(target->face)},
                                               {u"block"_s, block ? m_session.content()->block(*block).name : QString()}});
    }
}

QJsonValue QcControlReports::stackJson(QcItemStack stack) const
{
    const auto content = m_session.content();
    if (stack.isEmpty() || !content || !content->isValidItem(stack.item)) {
        return QJsonValue();
    }

    QJsonObject json{{u"item"_s, content->item(stack.item).name}, {u"count"_s, stack.count}};
    if (stack.damage > 0) {
        json.insert(u"damage"_s, stack.damage);
    }
    return json;
}

QJsonObject QcControlReports::inventoryJson() const
{
    QJsonArray stacks;
    for (int i = 0; i < QcInventory::slotCount; ++i) {
        const QJsonValue stack = stackJson(m_session.inventory().slot(i));
        if (!stack.isNull()) {
            QJsonObject entry = stack.toObject();
            entry.insert(u"slot"_s, i);
            stacks.append(entry);
        }
    }

    QJsonArray grid;
    for (qsizetype i = 0; i < m_session.craftingGrid().size(); ++i) {
        const QJsonValue stack = stackJson(m_session.craftingGrid()[i]);
        if (!stack.isNull()) {
            QJsonObject entry = stack.toObject();
            entry.insert(u"slot"_s, int(36 + i));
            grid.append(entry);
        }
    }

    return {{u"selectedSlot"_s, m_session.inventory().selectedHotbarSlot()},
            {u"slots"_s, stacks},
            {u"craftingSize"_s, m_session.craftingSize()},
            {u"craftingGrid"_s, grid},
            {u"craftingResult"_s, stackJson(m_session.craftingResult())}};
}

QJsonObject QcControlReports::machineJson() const
{
    const std::optional<QcBlockPos> pos = m_session.machinePos();
    if (!pos) {
        return {{u"open"_s, false}};
    }

    const QList<QcItemStack> &stacks = m_session.machineSlots();
    if (m_session.containerOpen()) {
        QJsonArray slotList;
        for (const QcItemStack &stack : stacks) {
            slotList.append(stackJson(stack));
        }
        return {{u"open"_s, true}, {u"position"_s, QJsonArray{pos->x, pos->y, pos->z}}, {u"chest"_s, true}, {u"slots"_s, slotList}};
    }

    return {{u"open"_s, true},
            {u"position"_s, QJsonArray{pos->x, pos->y, pos->z}},
            {u"category"_s, m_session.machineCategory()},
            {u"input"_s, stackJson(stacks.value(0))},
            {u"fuel"_s, stackJson(stacks.value(1))},
            {u"output"_s, stackJson(stacks.value(2))},
            {u"burn"_s, m_session.machineBurn()},
            {u"progress"_s, m_session.machineProgress()}};
}

QJsonObject QcControlReports::soundsJson() const
{
    // Loudness and side as the audio output would play them from where the player is now.
    const auto content = m_session.content();
    const QcVec3 ears = m_session.playerMotion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};

    QJsonArray sounds;
    for (const QcSoundCue &cue : m_session.recentSounds()) {
        const QcSoundDefinition *definition = content ? content->sound(cue.key) : nullptr;
        QJsonObject entry{{u"key"_s, cue.key},
                          {u"origin"_s, cue.origin == QcSoundCue::Origin::Server ? u"server"_s : u"local"_s},
                          {u"position"_s, vectorJson(cue.position)},
                          {u"volume"_s, cue.volume},
                          {u"pitch"_s, cue.pitch},
                          {u"tick"_s, qint64(cue.tick)},
                          {u"known"_s, definition != nullptr}};

        if (definition) {
            const QcSoundSpatial::Placement placement = QcSoundSpatial::place(
                ears, m_session.intent().yaw, cue.position, definition->range * std::max(1.0, double(cue.volume)));
            entry.insert(u"distance"_s, placement.distance);
            entry.insert(u"gain"_s, placement.gain * cue.volume * definition->volume);
            entry.insert(u"pan"_s, placement.pan);
        }

        sounds.append(entry);
    }

    const QcAmbience::Levels &ambience = m_session.ambience();
    return {{u"sounds"_s, sounds},
            {u"ambience"_s, QJsonObject{{u"wind"_s, ambience.wind}, {u"cave"_s, ambience.cave}, {u"water"_s, ambience.water}, {u"rain"_s, ambience.rain}}}};
}
