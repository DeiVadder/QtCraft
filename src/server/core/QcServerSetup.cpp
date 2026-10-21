#include "server/core/QcServerSetup.h"

#include "common/physics/QcPhysics.h"
#include "server/blocks/QcBlockUpdateQueue.h"
#include "server/blocks/QcRails.h"
#include "server/core/QcServerLogging.h"
#include "server/core/QcServerState.h"
#include "server/gameplay/QcStandProperties.h"
#include "server/hanging/QcHangingRules.h"
#include "server/hanging/QcServerHanging.h"
#include "server/maps/QcMapSurvey.h"
#include "server/minecarts/QcMinecartMotion.h"
#include "server/mobs/QcPetCare.h"


#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

QcBlockId blockAt(QcServerState &state, QcBlockPos pos)
{
    const std::shared_ptr<const QcChunk> chunk = state.world().loadChunk(pos.chunk());
    return chunk->block(pos.localX(), pos.y, pos.localZ());
}

QList<QcItemStack> stacksOf(const QcServerState &state, const QList<QcStructureDefinition::Stack> &items)
{
    QList<QcItemStack> stacks;
    for (const QcStructureDefinition::Stack &item : items) {
        const std::optional<QcItemId> id = state.content().itemId(item.item);
        if (!id) {
            qCWarning(lcServer) << "World setup: unknown item" << item.item;
            continue;
        }
        stacks.append({*id, std::min(item.count, state.content().item(*id).maxStack), 0});
    }
    return stacks;
}

// A chest gets the stacks in its slots, a machine as input, fuel and output, a campfire one item per
// place to cook (decision E-772).
void fill(QcServerState &state, const QcWorldSetup::Contents &contents)
{
    const QcBlockId block = blockAt(state, contents.at);
    const QList<QcItemStack> stacks = stacksOf(state, contents.items);
    if (const int places = state.world().campfires().slotsOf(block); places > 0) {
        bool room = true;
        for (const QcItemStack &stack : stacks) {
            for (int item = 0; item < stack.count && room; ++item) {
                room = state.world().campfires().put(contents.at, places, stack.item);
            }
        }
        return;
    }

    QcBlockEntities &entities = state.blockEntities();
    if (!entities.ensure(contents.at, block)) {
        qCWarning(lcServer) << "World setup: no chest or machine for contents at" << contents.at.x << contents.at.y << contents.at.z;
        return;
    }

    if (QcBlockEntities::Container *container = entities.container(contents.at)) {
        for (qsizetype i = 0; i < stacks.size() && i < container->stacks.size(); ++i) {
            container->stacks[i] = stacks[i];
        }
    } else if (QcBlockEntities::Machine *machine = entities.machine(contents.at)) {
        for (qsizetype i = 0; i < stacks.size() && i < QcBlockEntities::Machine::slotCount; ++i) {
            machine->stacks[std::size_t(i)] = stacks[i];
        }
    }
    state.noteContents(contents.at);
}

// The text of a sign of a structure, cleaned like a player's, in its colour.
void write(QcServerState &state, const QcWorldSetup::Sign &sign)
{
    const QcBlockId block = blockAt(state, sign.at);
    if (!state.world().signs().isSign(block)) {
        qCWarning(lcServer) << "World setup: no sign for text at" << sign.at.x << sign.at.y << sign.at.z;
        return;
    }
    QcSignText::Lines lines;
    for (qsizetype line = 0; line < sign.lines.size() && line < QcSignText::maxLines; ++line) {
        lines[std::size_t(line)] = sign.lines[line];
    }
    const QcSignDefinition &definition = state.content().block(block).sign;
    state.world().signs().set(sign.at, QcSignText::cleaned(lines, definition.lines, definition.lineLength));
    state.world().signs().setColor(sign.at, sign.color);
}

// An armour stand of a structure; properties that do not apply are reported and left out.
void placeStand(QcServerState &state, const QcWorldSetup::Stand &setup)
{
    QcArmourStand stand;
    stand.id = state.entities().newId();
    stand.motion.position = setup.at;
    stand.yaw = qcNormalizedAngle(setup.yaw);
    stand.item = QcStandProperties::standItem(state.content());
    for (const QString &property : setup.properties) {
        if (!QcStandProperties::applyLine(state.content(), stand, property)) {
            qCWarning(lcServer) << "World setup: armour stand property" << property << "does not apply";
        }
    }
    if (!state.entities().stands().add(stand)) {
        qCWarning(lcServer) << "World setup: too many armour stands";
    }
}

// The map grid square of the first framed map of a setup (decision E-811): the maps of a wall map
// lie east and south of it.
std::optional<QPoint> wallMapOrigin(const QList<QcWorldSetup::Hanging> &hangings)
{
    const auto framed = std::ranges::find_if(hangings, [](const QcWorldSetup::Hanging &hanging) { return hanging.map.has_value(); });
    if (framed == hangings.end() || !framed->map) {
        return std::nullopt;
    }
    const QPoint offset = *framed->map * QcMapData::size;
    return QcMapData::around(framed->at.x - offset.x(), framed->at.z - offset.y(), 0).centre();
}

// A new map of scale 0 around `centre`, revealed over its whole area: the setup generates the
// area's chunks first (the survey reads loaded chunks only; decision E-811). Its stack, or an empty
// one when the ids are used up.
QcItemStack revealedMap(QcServerState &state, QcItemId filledMap, QPoint centre)
{
    const QcMapData map = QcMapData::around(centre.x(), centre.y(), 0);
    const std::optional<quint32> id = state.maps().create(map);
    if (!id) {
        qCWarning(lcServer) << "World setup: no map ids left";
        return {};
    }
    const int half = QcMapData::size / 2;
    const QcChunkPos first = QcBlockPos{map.centre().x() - half, 0, map.centre().y() - half}.chunk();
    const QcChunkPos last = QcBlockPos{map.centre().x() + half - 1, 0, map.centre().y() + half - 1}.chunk();
    for (int x = first.x; x <= last.x; ++x) {
        for (int z = first.z; z <= last.z; ++z) {
            state.world().loadChunk({x, z});
        }
    }

    QList<QPoint> pixels;
    pixels.reserve(QcMapData::pixelCount);
    for (int x = 0; x < QcMapData::size; ++x) {
        for (int z = 0; z < QcMapData::size; ++z) {
            pixels.append({x, z});
        }
    }
    const QList<QcMapSurvey::Surveyed> surveyed = QcMapSurvey::survey(state.world(), map, pixels);
    for (const QcMapSurvey::Surveyed &pixel : surveyed) {
        state.maps().set(*id, pixel.pixel, pixel.value);
    }
    return QcItemStack{filledMap, 1, 0}.withMapId(*id);
}

// An item frame or painting of the setup (decision E-797); one whose item, held item or motif the
// content does not know, that has no wall or room there or that covers another is reported and
// left out. Blocks not loaded count as holding, like for those already hanging.
// A framed map of a wall map (decision E-811) is made around `wallMap` plus its offset.
void placeHanging(QcServerState &state, const QcWorldSetup::Hanging &setup, std::optional<QPoint> wallMap)
{
    const QcContentRegistry &content = state.content();
    const std::optional<QcItemId> item = content.itemId(setup.item);
    const std::optional<QcHangingDefinition> &hangs = item ? content.item(*item).hangs : std::nullopt;
    if (!item || !hangs) {
        qCWarning(lcServer) << "World setup: no frame or painting item" << setup.item;
        return;
    }

    QcHangingEntity hanging;
    hanging.item = content.item(*item).name;
    hanging.hanging.type = hangs->type;
    hanging.hanging.face = setup.face;
    hanging.hanging.cell = setup.at;
    hanging.hanging.rotation = quint8(setup.rotation);
    if (hangs->type == QcHangingType::Painting) {
        const auto motif = std::ranges::find(hangs->motifs, setup.motif, &QcPaintingMotif::name);
        if (motif == hangs->motifs.end()) {
            qCWarning(lcServer) << "World setup: no painting motif" << setup.motif;
            return;
        }
        hanging.hanging.motif = quint8(motif - hangs->motifs.begin());
        hanging.hanging.width = motif->width;
        hanging.hanging.height = motif->height;
    } else if (!setup.held.isEmpty()) {
        const std::optional<QcItemId> held = content.itemId(setup.held);
        if (!held) {
            qCWarning(lcServer) << "World setup: unknown item in a frame" << setup.held;
            return;
        }
        hanging.held = {*held, 1, 0};
        if (setup.map && content.item(*held).map != QcMapItem::Filled) {
            qCWarning(lcServer) << "World setup: a wall map's frame holds no filled map but" << setup.held;
        }
    }

    if (!QcHangingRules::holds(state.world(), hanging.hanging).value_or(true) || QcHangingRules::overlaps(state.entities().hangings(), hanging.hanging)) {
        qCWarning(lcServer) << "World setup: no room for" << setup.item << "at" << setup.at.x << setup.at.y << setup.at.z;
        return;
    }
    // The map is made only for a frame that hangs.
    if (setup.map && wallMap && !hanging.held.isEmpty() && content.item(hanging.held.item).map == QcMapItem::Filled) {
        hanging.held = revealedMap(state, hanging.held.item, *wallMap + *setup.map * QcMapData::size);
    }
    if (!QcServerHanging::spawn(state, hanging)) {
        qCWarning(lcServer) << "World setup: too many item frames and paintings";
    }
}

// A minecart of the setup standing in the middle of its rail.
void placeMinecart(QcServerState &state, const QcWorldSetup::Cart &setup)
{
    const QcBlockPos cell = setup.at;
    const std::optional<QcRails::Track> track = QcRails::trackOf(state.content(), blockAt(state, cell));
    const std::optional<QcItemId> item = state.content().itemId(setup.item);
    if (!track || !item || !state.content().item(*item).placesCart) {
        qCWarning(lcServer) << "World setup: no rail or no cart item for a minecart at" << cell.x << cell.y << cell.z << setup.item;
        return;
    }

    QcMinecart cart = state.entities().minecarts().fromItem(state.content().item(*item).name);
    cart.id = state.entities().newId();
    const QcVec3 from = QcRails::endPoint(cell, track->ends[0]);
    const QcVec3 to = QcRails::endPoint(cell, track->ends[1]);
    cart.motion.position = (from + to) * 0.5;
    cart.motion.onGround = true;
    cart.yaw = QcMinecartMotion::yawAlong(to - from);
    if (!state.entities().minecarts().add(cart)) {
        qCWarning(lcServer) << "World setup: too many minecarts";
    }
}

// Contents at a rail with a container cart go into the cart (decision E-386); true when they did.
bool fillCart(QcServerState &state, const QcWorldSetup::Contents &contents)
{
    for (QcMinecart &cart : state.entities().minecarts().editableAll()) {
        if (cart.stacks.isEmpty() || QcPhysics::blockContaining(cart.motion.position) != contents.at) {
            continue;
        }
        const QList<QcItemStack> stacks = stacksOf(state, contents.items);
        for (qsizetype i = 0; i < stacks.size() && i < cart.stacks.size(); ++i) {
            cart.stacks[i] = stacks[i];
        }
        return true;
    }
    return false;
}

} // namespace

namespace QcServerSetup {

void apply(QcServerState &state, QcBlockUpdateQueue &updates, const QcWorldSetup &setup)
{
    if (setup.timeOfDay) {
        state.info().worldTime = *setup.timeOfDay;
    }
    state.info().daylightCycle = setup.daylightCycle;
    state.info().mobSpawning = setup.mobSpawning;
    state.info().randomTicks = setup.randomTicks;
    state.info().fireSpread = setup.fireSpread;
    state.info().weatherCycle = setup.weatherCycle;
    state.info().mobGriefing = setup.mobGriefing;
    state.info().explosionBlockDamage = setup.explosionBlockDamage;
    state.info().adventure.merge(setup.adventure);

    for (const QcWorldSetup::Mob &mob : setup.mobs) {
        if (const std::optional<int> type = state.content().mobIndex(mob.mob)) {
            const quint32 id = state.entities().creatures().spawn(*type, mob.at, state.entities().newId(), false, true);
            // Villagers of a station come rested, as if they had just slept: a golem farm of the
            // showcase summons at once although its fixed time of day never brings a night
            // (decision E-470).
            QcCreature &creature = *state.entities().creatures().find(id);
            creature.confined = mob.confined;
            creature.noAi = mob.noAi;
            if (creature.isVillager()) {
                creature.villager->sleptAt = state.info().worldTime;
            }
            // A pet of a made-up owner (the showcase's pet corner, decision E-480).
            if (!mob.owner.isEmpty() && state.content().mobs()[*type].tameable) {
                QcPetCare::tame(state.entities(), state.content(), creature, mob.owner);
                creature.tame->sitting = mob.sitting;
                creature.pose = mob.sitting ? QcPose::Sitting : QcPose::Standing;
            }
        } else {
            qCWarning(lcServer) << "World setup: unknown creature" << mob.mob;
        }
    }

    for (const QcWorldSetup::Stand &stand : setup.stands) {
        placeStand(state, stand);
    }

    for (const QcWorldSetup::Cart &cart : setup.minecarts) {
        placeMinecart(state, cart);
    }
    for (const QcWorldSetup::Contents &contents : setup.contents) {
        if (!fillCart(state, contents)) {
            fill(state, contents);
        }
    }
    for (const QcBlockPos &pos : setup.growing) {
        state.plants().saplingPlaced(state.blockEntities(), pos, blockAt(state, pos));
    }
    for (const QcWorldSetup::Sign &sign : setup.signs) {
        write(state, sign);
    }
    const std::optional<QPoint> wallMap = wallMapOrigin(setup.hangings);
    for (const QcWorldSetup::Hanging &hanging : setup.hangings) {
        placeHanging(state, hanging, wallMap);
    }

    updates.setCommandMode(true);
    for (const QcBlockPos &pos : setup.updates) {
        updates.queue(pos);
        // Circuits of demos start too (decision E-340).
        state.circuits().wake(state.world(), pos);
    }
    updates.setCommandMode(false);
    state.info().setupDone = true;
}

} // namespace QcServerSetup
