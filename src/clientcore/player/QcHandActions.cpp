#include "clientcore/player/QcHandActions.h"

#include "clientcore/player/QcClientGameMode.h"
#include "clientcore/player/QcClientInventory.h"
#include "clientcore/player/QcPrediction.h"
#include "clientcore/session/QcClientEntities.h"
#include "clientcore/session/QcClientWorld.h"
#include "common/content/definitions/QcSoundKeys.h"
#include "common/gameplay/QcMining.h"
#include "common/physics/QcPhysics.h"
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

// Whether the entity takes every use, also with an empty hand: an armour stand, a minecart, a
// villager (its trading screen, decision E-414) and a tameable creature (taming, sitting, healing,
// its collar; decision E-480), the last two not while sneaking.
bool takesEveryUse(const QcContentRegistry &content, const QcClientEntity &entity, bool sneaking)
{
    const QcMobDefinition *creature = entity.creature(content);
    return entity.isArmourStand() || entity.isMinecart() || (creature && (creature->villager || creature->tameable) && !sneaking);
}

// Whether using `block` (none: no block aimed at) reacts instead of acting on it: machines, chests,
// doors, levers, buttons, repeaters and crafting tables, unless the player sneaks.
bool reactsToUse(const QcBlockDefinition *block, bool sneaking)
{
    return block && !sneaking
           && (block->opensCrafting || block->machine.isMachine() || block->containerSlots > 0 || block->toggles
               || block->signal.isUsable());
}

// Whether `block` (none: no block aimed at) takes food used on it instead of the food being eaten:
// cake is eaten bite by bite, a lit campfire cooks it (decisions E-772, E-773); not while sneaking.
bool takesFood(const QcBlockDefinition *block, bool sneaking)
{
    return block && !sneaking && (block->bites.isEdible() || block->campfire.cooks());
}

// Whether the held item aims at liquids too: buckets and bottles take from them, a lily pad lies on
// them (decision E-774).
bool aimsAtLiquids(const QcContentRegistry &content, const QcItemDefinition *held)
{
    if (!held) {
        return false;
    }
    const std::optional<QcBlockId> placed = held->placesBlock.isEmpty() ? std::nullopt : content.blockId(held->placesBlock);
    return held->scoops || held->bottles || (placed && content.block(*placed).floats);
}

} // namespace

QcHandActions::QcHandActions(const View &view, QcClientGameMode &gameMode, Sender send, Cue cue)
    : m_view(view)
    , m_gameMode(gameMode)
    , m_send(std::move(send))
    , m_cue(std::move(cue))
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
    const double yaw = qDegreesToRadians(double(intent.yaw));
    const double pitch = qDegreesToRadians(double(intent.pitch));
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

QcHandActions::HandEffect QcHandActions::updateMining(const QcClientWorld &world, const QcContentRegistry &content)
{
    refreshTarget(world, content);

    const std::optional<QcBlockPos> wished = m_miningRequested && m_target ? std::optional<QcBlockPos>(m_target->block) : std::nullopt;
    // Adventure mode (decision E-735): a block the allow-lists do not name is not mined at all.
    const bool refused = wished && !permitted(world, content, QcAdventureRules::Action::Break, *wished);
    const std::optional<QcBlockPos> aimed = refused ? std::nullopt : wished;
    // Creative mode: strikes break at once, a held button again every few ticks (decision E-282);
    // a survival mining that still runs is cancelled below.
    const bool instant = m_gameMode.rules().instantBreak;
    HandEffect effect = HandEffect::None;
    if (const std::optional<QcBlockPos> broken = instant ? m_gameMode.nextInstantBreak(aimed) : std::nullopt) {
        m_send(QcMsg::StartMining{*broken});
        m_send(QcMsg::FinishMining{*broken});
        effect = HandEffect::Swing;
    }

    const std::optional<QcBlockPos> wanted = instant ? std::nullopt : aimed;
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
        return refused ? HandEffect::Refused : effect;
    }

    const QcBlockPos mining = *m_miningPos;
    const std::optional<QcBlockId> block = world.blockAt(mining);
    if (!block) {
        return effect;
    }

    const QcBlockDefinition &definition = content.block(*block);
    const double seconds = QcMining::breakSeconds(definition, m_view.inventory.heldDefinition(content));
    if (seconds <= 0.0) {
        return effect;
    }

    if (m_miningTicks++ % miningHitTicks == 0) {
        m_cue({QcSoundKeys::blockSound(definition, QcBlockSoundAction::Hit),
               {mining.x + 0.5, mining.y + 0.5, mining.z + 0.5}, 0.35f, 1.0f, QcSoundCue::Origin::Local, 0});
    }

    m_miningProgress += QcPhysics::tickDuration / seconds;
    if (m_miningProgress >= 1.0) {
        // The server decides; its BlockUpdate removes the block (or restores it on rejection).
        m_send(QcMsg::FinishMining{mining});
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
        return HandEffect::None;
    }

    // Adventure mode (decision E-735): the held item acts on the block only where the allow-lists
    // permit it; food is eaten instead (the server decides the same). An empty hand would do
    // nothing anyway: no request and no notice.
    if (!opensScreen && !permitted(world, content, QcAdventureRules::Action::PlaceOn, m_target->block)) {
        if (item && item->food > 0) {
            m_send(QcMsg::ConsumeItem{});
            return HandEffect::Use;
        }
        return item ? HandEffect::Refused : HandEffect::None;
    }

    const QcVec3 hit = m_target->point - QcVec3{double(m_target->block.x), double(m_target->block.y), double(m_target->block.z)};
    const auto clamp01 = [](double value) { return std::clamp(value, 0.0, 1.0); };
    m_send(QcMsg::UseItem{m_target->block, m_target->face, {clamp01(hit.x), clamp01(hit.y), clamp01(hit.z)}});
    return HandEffect::UsedBlock;
}

void QcHandActions::endUse(QcMsg::DrawLauncher::Action action, bool send)
{
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
