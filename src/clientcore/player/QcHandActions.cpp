// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/player/QcHandActions.h"

#include "clientcore/player/QcBlockForecast.h"
#include "clientcore/player/QcClientGameMode.h"
#include "clientcore/player/QcClientInventory.h"
#include "clientcore/player/QcPrediction.h"
#include "clientcore/session/QcClientEntities.h"
#include "clientcore/session/QcClientWorld.h"
#include "common/content/definitions/QcSoundKeys.h"
#include "common/gameplay/QcEnchanting.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
#include "common/world/QcBoatBody.h"
#include "common/world/QcMinecartBody.h"

#include <QtMath>

#include <algorithm>
#include <cmath>

namespace {

// The box the crosshair hits an entity in.
QcAabb targetBoxOf(const QcContentRegistry &content, const QcClientEntity &entity)
{
    if (entity.isMinecart()) {
        return QcPhysics::boxAt(entity.position, QcMinecartBody::width, QcMinecartBody::height);
    }
    if (entity.isBoat()) {
        return QcPhysics::boxAt(entity.position, QcBoatBody::width, QcBoatBody::height);
    }
    if (entity.isHanging()) {
        return entity.hanging.box();
    }
    if (entity.isArmourStand()) {
        const double scale = entity.stand.small ? 0.5 : 1.0;
        return QcPhysics::boxAt(entity.position, QcStandPose::bodyWidth * scale, QcStandPose::bodyHeight * scale);
    }

    // Young animals are drawn and aimed at half size (decision E-293).
    const QcMobDefinition *creature = entity.creature(content);
    const double scale = (entity.flags & QcMsg::Young) != 0 ? QcMsg::youngScale : 1.0;
    const double width = creature ? creature->width : QcPhysics::playerWidth;
    const double height = creature ? creature->height : QcPhysics::playerHeight;
    // A villager in its bed is aimed at where it lies (decisions E-411, E-423).
    return QcPoses::boxAt(QcMsg::poseOf(entity.flags), entity.position, entity.yaw, width * scale, height * scale);
}

// Whether using `item` on the entity is a use of the creature (UseEntity): food, armour, its
// catching item, its milking item (decision E-500) or its breeding food (decision E-293).
bool usesOnCreature(const QcContentRegistry &content, const QcClientEntity &entity, const QcItemDefinition &item)
{
    const QcMobDefinition *creature = entity.creature(content);
    return creature && QcClientLauncher::usesOnCreature(*creature, item);
}

// Whether the entity takes every use, also with an empty hand: an armour stand, a minecart, a boat
// (decision E-920), an item frame or painting (decision E-790), a
// villager (its trading screen, decision E-414) and a tameable creature (taming, sitting, healing,
// its collar; decision E-480), the last two not while sneaking.
bool takesEveryUse(const QcContentRegistry &content, const QcClientEntity &entity, bool sneaking)
{
    const QcMobDefinition *creature = entity.creature(content);
    return entity.isArmourStand() || entity.isMinecart() || entity.isBoat() || entity.isHanging() || (creature && (creature->villager || creature->tameable) && !sneaking);
}

// Whether using `block` (none: no block aimed at) reacts instead of acting on it: machines, chests,
// doors, levers, buttons, repeaters, crafting tables and signs, unless the player sneaks.
bool reactsToUse(const QcBlockDefinition *block, bool sneaking)
{
    return block && !sneaking
           && (block->opensCrafting || block->opensStonecutter || block->opensEnchanting || block->opensAnvil || block->machine.isMachine() || block->containerSlots > 0 || block->toggles
               || block->signal.isUsable() || block->sign.isSign());
}

// Whether `block` (none: no block aimed at) takes food used on it instead of the food being eaten:
// cake is eaten bite by bite, a lit campfire cooks it (decisions E-772, E-773); not while sneaking.
bool takesFood(const QcBlockDefinition *block, bool sneaking)
{
    return block && !sneaking && (block->bites.isEdible() || block->campfire.cooks());
}

// Whether the held item aims at liquids too: buckets and bottles take from them, a lily pad and a
// boat lie on them (decisions E-774, E-920).
bool aimsAtLiquids(const QcContentRegistry &content, const QcItemDefinition *held)
{
    if (!held) {
        return false;
    }
    const std::optional<QcBlockId> placed = held->placesBlock.isEmpty() ? std::nullopt : content.blockId(held->placesBlock);
    return held->scoops || held->bottles || held->placesBoat() || (placed && content.block(*placed).floats);
}

} // namespace

QcHandActions::QcHandActions(const View &view, QcClientGameMode &gameMode, Sender send, Cue cue, Predictor predict)
    : m_view(view)
    , m_gameMode(gameMode)
    , m_send(std::move(send))
    , m_cue(std::move(cue))
    , m_predict(std::move(predict))
{
}

void QcHandActions::refreshTarget(const QcClientWorld &world, const QcContentRegistry &content)
{
    // A spectator only looks (decision E-730): no block or entity is targeted, so nothing is
    // outlined, mined, used or attacked.
    if (!m_gameMode.rules().interacts) {
        m_target.reset();
        m_targetEntity.reset();
        return;
    }

    const QcPlayerInput &intent = m_view.prediction.intent();
    const double yaw = qDegreesToRadians(static_cast<double>(intent.yaw));
    const double pitch = qDegreesToRadians(static_cast<double>(intent.pitch));
    const QcVec3 direction{std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)};
    const QcVec3 eye = m_view.prediction.motion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};

    const QcItemDefinition *held = m_view.inventory.heldDefinition(content);
    m_target = QcRaycast::cast(world, eye, direction, QcRaycast::playerReach,
                               aimsAtLiquids(content, held) ? QcRaycast::Targets::VisibleAndLiquids : QcRaycast::Targets::Visible);

    m_targetEntity.reset();
    double nearest = m_target ? m_target->distance : attackReach;
    const quint32 vehicle = m_view.prediction.riding().vehicle();
    for (const QcClientEntity &entity : m_view.entities.all()) {
        if (entity.isItem() || entity.isExperienceOrb() || entity.isProjectile() || entity.isBlockEntity() || entity.id == vehicle) {
            continue;
        }

        const std::optional<double> distance =
            QcRaycast::intersectBox(eye, direction, targetBoxOf(content, entity), std::min(nearest, attackReach));

        if (distance && *distance < nearest) {
            nearest = *distance;
            m_targetEntity = entity.id;
        }
    }
}

QcHandActions::Aimed QcHandActions::aimedForMining(const QcClientWorld &world, const QcContentRegistry &content) const
{
    const std::optional<QcBlockPos> wished = m_miningRequested && m_target ? std::optional<QcBlockPos>(m_target->block) : std::nullopt;
    // Adventure mode (decision E-735): a block the allow-lists do not name is not mined at all.
    const bool refused = wished && !permitted(world, content, QcAdventureRules::Action::Break, *wished);
    return {refused ? std::nullopt : wished, refused};
}

QcHandActions::HandEffect QcHandActions::breakInstantly(const QcClientWorld &world, std::optional<QcBlockPos> aimed)
{
    const std::optional<QcBlockPos> broken = m_gameMode.nextInstantBreak(aimed);
    if (!broken) {
        return HandEffect::None;
    }

    m_send(QcMsg::StartMining{*broken});
    finishMining(world, *broken);
    return HandEffect::Swing;
}

void QcHandActions::finishMining(const QcClientWorld &world, QcBlockPos pos)
{
    // The block goes at once (decision E-853); the server confirms or puts it back.
    const quint32 sequence = m_predict(QcBlockForecast::breaking(world, pos));
    m_send(QcMsg::FinishMining{pos, sequence});
}

void QcHandActions::setMining(bool mining)
{
    m_miningRequested = mining;
    if (!mining) {
        // Without this, a press within a tick of the release waited for the rest of the pace.
        m_gameMode.releaseInstantBreak();
    }
}

QcHandActions::HandEffect QcHandActions::strike(const QcClientWorld &world, const QcContentRegistry &content)
{
    if (!m_gameMode.rules().instantBreak) {
        return HandEffect::None;
    }

    refreshTarget(world, content);
    const Aimed aimed = aimedForMining(world, content);
    return aimed.block ? breakInstantly(world, aimed.block) : HandEffect::None;
}

QcHandActions::HandEffect QcHandActions::updateMining(const QcClientWorld &world, const QcContentRegistry &content)
{
    refreshTarget(world, content);

    const Aimed aimed = aimedForMining(world, content);
    // Creative mode: strikes break at once, a held button again every few ticks (decision E-282);
    // a survival mining that still runs is cancelled below.
    const bool instant = m_gameMode.rules().instantBreak;
    const HandEffect effect = instant ? breakInstantly(world, aimed.block) : HandEffect::None;

    const std::optional<QcBlockPos> wanted = instant ? std::nullopt : aimed.block;
    if (wanted != m_miningPos) {
        if (m_miningPos) {
            m_send(QcMsg::CancelMining{});
        }
        m_miningPos = wanted;
        m_miningProgress = 0.0;
        m_miningTicks = 0;
        if (m_miningPos) {
            m_send(QcMsg::StartMining{*m_miningPos});
        }
    }

    if (!m_miningPos) {
        return aimed.refused ? HandEffect::Refused : effect;
    }

    const QcBlockPos mining = *m_miningPos;
    const std::optional<QcBlockId> block = world.blockAt(mining);
    if (!block) {
        return effect;
    }

    const QcBlockDefinition &definition = content.block(*block);
    // Efficiency speeds a matching tool up (decision E-864), as the server counts it.
    const int efficiency = QcEnchanting::levelOf(content, m_view.inventory.inventory().selectedStack(), QcEnchantmentEffect::Efficiency);
    const double seconds = QcMining::breakSeconds(definition, m_view.inventory.heldDefinition(content), QcEnchanting::speedBonus(efficiency));
    if (seconds <= 0.0) {
        return effect;
    }

    if (m_miningTicks++ % miningHitTicks == 0) {
        m_cue({QcSoundKeys::blockSound(definition, QcBlockSoundAction::Hit),
               {mining.x + 0.5, mining.y + 0.5, mining.z + 0.5}, 0.35f, 1.0f, QcSoundCue::Origin::Local, 0});
    }

    m_miningProgress += QcPhysics::tickDuration / seconds;
    if (m_miningProgress >= 1.0) {
        // Survival mode predicts the break only now, at the end of the mining (decision E-853).
        finishMining(world, mining);
        m_miningPos.reset();
        m_miningProgress = 0.0;
    }
    return effect;
}

void QcHandActions::tickLauncher(const QcContentRegistry &content, double serverTicks)
{
    m_launcher.tick(m_view.inventory.heldDefinition(content), m_view.inventory.inventory().selectedHotbarSlot(), serverTicks);
}

bool QcHandActions::permitted(const QcClientWorld &world, const QcContentRegistry &content, QcAdventureRules::Action action, QcBlockPos pos) const
{
    if (!m_gameMode.rules().buildByAllowList) {
        return true;
    }

    const std::optional<QcBlockId> block = world.blockAt(pos);
    return block && content.isValidBlock(*block)
           && m_gameMode.adventure().permits(m_gameMode.mode(), action, m_view.inventory.heldDefinition(content), content.block(*block));
}

QcHandActions::HandEffect QcHandActions::useOnEntity(const QcContentRegistry &content, const QcItemDefinition *item)
{
    if (!m_targetEntity) {
        return HandEffect::None;
    }

    // An armour stand takes or gives armour and items, also with an empty hand; a minecart takes the
    // player in or opens its screen; a villager opens its trading screen (decision E-414).
    const QcClientEntity targeted = m_view.entities.all().value(*m_targetEntity);
    if (takesEveryUse(content, targeted, m_view.prediction.intent().sneak)) {
        m_send(QcMsg::UseEntity{*m_targetEntity});
        return HandEffect::UsedEntity;
    }

    // Food, armour and what a creature takes (its catching item, its breeding food) used on a creature
    // go to the server, which feeds or catches it or else eats the food or puts on the armour
    // (decisions E-057, E-293); other items act on the block behind it.
    const auto creature = m_view.entities.all().constFind(*m_targetEntity);
    if (item && creature != m_view.entities.all().constEnd() && usesOnCreature(content, *creature, *item)) {
        m_send(QcMsg::UseEntity{creature.key()});
        return HandEffect::Use;
    }
    return HandEffect::None;
}

QcHandActions::HandEffect QcHandActions::use(const QcClientWorld &world, const QcContentRegistry &content)
{
    if (!m_gameMode.rules().interacts) {
        return HandEffect::None;
    }

    const QcItemDefinition *item = m_view.inventory.heldDefinition(content);

    // Machines, chests, doors, levers, buttons, repeaters and crafting tables react when used (unless sneaking);
    // otherwise food without a block to place is eaten and everything else acts on the targeted block.
    const std::optional<QcBlockId> targetBlock = m_target ? world.blockAt(m_target->block) : std::nullopt;
    const QcBlockDefinition *targetDefinition = targetBlock ? &content.block(*targetBlock) : nullptr;
    // Out of the server's reach (QcRaycast::isWithinReach) a block does not react, as on the server.
    const QcVec3 eye = m_view.prediction.motion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    const bool opensScreen = m_target && reactsToUse(targetDefinition, m_view.prediction.intent().sneak) && QcRaycast::isWithinReach(eye, m_target->block);
    if (const HandEffect onEntity = useOnEntity(content, item); onEntity != HandEffect::None) {
        return onEntity;
    }

    // A launcher draws while the button is held (decision E-425), releaseUse shoots; a throwable
    // item is thrown at once (decision E-427).
    if (item && !opensScreen && QcClientLauncher::uses(*item)) {
        const QcInventory &inventory = m_view.inventory.inventory();
        const std::optional<QcMessage> message =
            m_launcher.use(m_view.inventory.heldItem(), *item, inventory, content, m_gameMode.rules().infiniteItems);
        if (message) {
            m_send(*message);
        }
        // A throw swings the arm from the start (swingAfter gives 0), a draw leaves the swing as it is.
        constexpr int unchanged = -1;
        return QcClientLauncher::swingAfter(message, unchanged) == 0 ? HandEffect::Swing : HandEffect::Handled;
    }

    // Food is eaten, drinks drunk (decision E-500) and armour put on (all sent as ConsumeItem) unless
    // the food plants a block and a block is aimed at, or the aimed block takes food (cake, a
    // campfire): then the server plants, cooks or eats it or, where it cannot, eats the food (decision
    // E-292).
    const bool fed = m_target && takesFood(targetDefinition, m_view.prediction.intent().sneak) && QcRaycast::isWithinReach(eye, m_target->block);
    if (item && QcClientLauncher::consumes(*item, m_target.has_value()) && !opensScreen && !fed) {
        m_send(QcMsg::ConsumeItem{});
        return HandEffect::Use;
    }

    if (!m_target) {
        // A backpack used in the air opens in the hand (decision E-833); the server answers with its
        // screen, so it shows like a block's.
        if (item && item->portableContainer) {
            m_send(QcMsg::ConsumeItem{});
            return HandEffect::UsedBlock;
        }
        return HandEffect::None;
    }
    const QcRaycastHit target = *m_target;

    // Adventure mode (decision E-735): the held item acts on the block only where the allow-lists
    // permit it; food is eaten instead (the server decides the same). An empty hand would do
    // nothing anyway: no request and no notice.
    if (!opensScreen && !permitted(world, content, QcAdventureRules::Action::PlaceOn, target.block)) {
        if (item && item->food > 0) {
            m_send(QcMsg::ConsumeItem{});
            return HandEffect::Use;
        }
        return item ? HandEffect::Refused : HandEffect::None;
    }

    const QcVec3 hit = hitInside(target);
    const QcPlayerInput &intent = m_view.prediction.intent();
    const quint32 sequence = predictPlacement(world, content, target, hit);
    m_send(QcMsg::UseItem{target.block, target.face, hit, sequence, QcMsg::UseItem::View{intent.yaw, intent.pitch}});
    return HandEffect::UsedBlock;
}

quint32 QcHandActions::predictPlacement(const QcClientWorld &world, const QcContentRegistry &content, const QcRaycastHit &target, const QcVec3 &hit)
{
    return m_predict(placementForecast(world, content, target, hit));
}

QList<QcMsg::BlockUpdate> QcHandActions::placementForecast(const QcClientWorld &world, const QcContentRegistry &content, const QcRaycastHit &target,
                                                           const QcVec3 &hit) const
{
    // A block that reacts to the use (a screen, a door, a lever, cake, a campfire) takes it on the
    // server, also out of reach, where the client would not open it: nothing is placed then.
    const QcItemDefinition *item = m_view.inventory.heldDefinition(content);
    const std::optional<QcBlockId> clicked = world.blockAt(target.block);
    const QcPlayerInput &intent = m_view.prediction.intent();
    const QcBlockDefinition *clickedDefinition = clicked && content.isValidBlock(*clicked) ? &content.block(*clicked) : nullptr;
    if (!item || !clickedDefinition || reactsToUse(clickedDefinition, intent.sneak) || takesFood(clickedDefinition, intent.sneak)) {
        return {};
    }

    const QcBlockRules::Aim aim{target.block, target.face, hit, intent.yaw, intent.pitch};
    const QcVec3 eye = m_view.prediction.motion().position + QcVec3{0.0, QcPhysics::playerEyeHeight, 0.0};
    return QcBlockForecast::placing(world, *item, aim, eye, livingBodies(content));
}

QcVec3 QcHandActions::hitInside(const QcRaycastHit &target)
{
    const QcVec3 point = target.point - QcVec3{static_cast<double>(target.block.x), static_cast<double>(target.block.y), static_cast<double>(target.block.z)};
    const auto clamp01 = [](double value) { return std::clamp(value, 0.0, 1.0); };
    return QcVec3{clamp01(point.x), clamp01(point.y), clamp01(point.z)};
}

QList<QcAabb> QcHandActions::livingBodies(const QcContentRegistry &content) const
{
    QList<QcAabb> bodies{QcPhysics::boxAt(m_view.prediction.motion().position, QcPhysics::playerWidth, QcPhysics::playerHeight)};
    for (const QcClientEntity &entity : m_view.entities.all()) {
        if (entity.isPlayer() || entity.creature(content)) {
            bodies.append(targetBoxOf(content, entity));
        }
    }
    return bodies;
}

void QcHandActions::holdUse()
{
    m_useHeld = true;
    m_useTicks = 0;
}

QcHandActions::HandEffect QcHandActions::tickUse(const QcClientWorld &world, const QcContentRegistry &content)
{
    // While the mining button is held, the use does not repeat (as in the role model, which skips the
    // use while destroying); the pace starts again once the mining ends.
    if (!m_useHeld || m_miningRequested || m_miningPos) {
        return HandEffect::None;
    }
    if (++m_useTicks < useRepeatTicks) {
        return HandEffect::None;
    }

    // The next try comes useRepeatTicks later, whether this one used anything or not.
    m_useTicks = 0;
    if (!repeatsUse(world, content)) {
        return HandEffect::None;
    }
    return use(world, content);
}

bool QcHandActions::repeatsUse(const QcClientWorld &world, const QcContentRegistry &content) const
{
    const QcItemDefinition *item = m_view.inventory.heldDefinition(content);
    if (!item || !m_target || m_targetEntity || QcClientLauncher::uses(*item) || item->scoops || item->bottles || item->food > 0) {
        return false;
    }

    const std::optional<QcBlockId> block = world.blockAt(m_target->block);
    const QcBlockDefinition *definition = block && content.isValidBlock(*block) ? &content.block(*block) : nullptr;
    const bool sneaking = m_view.prediction.intent().sneak;
    if (!definition || reactsToUse(definition, sneaking) || takesFood(definition, sneaking) || QcClientLauncher::consumes(*item, true)) {
        return false;
    }

    // Only a use the client forecasts as a placed block repeats: where nothing would be placed the
    // server could act otherwise (planting food is eaten where it cannot be planted, decision E-292).
    return !placementForecast(world, content, *m_target, hitInside(*m_target)).isEmpty();
}

void QcHandActions::endUse(QcMsg::DrawLauncher::Action action, bool send)
{
    m_useHeld = false;
    if (m_launcher.release() && send) {
        m_send(QcMsg::DrawLauncher{action});
    }
}

QcHandActions::HandEffect QcHandActions::attack()
{
    if (!m_targetEntity) {
        return HandEffect::None;
    }

    m_send(QcMsg::AttackEntity{*m_targetEntity});
    return HandEffect::Swing;
}
