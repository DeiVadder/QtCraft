#include "common/handbook/builder/QcHandbookBlockTables.h"

#include "common/content/registry/QcContentRegistry.h"
#include "common/handbook/builder/QcHandbookContext.h"
#include "common/handbook/builder/QcHandbookOverviewPages.h"
#include "common/handbook/builder/QcHandbookText.h"
#include "common/handbook/QcHandbookLinks.h"
#include "common/physics/QcSpawnGround.h"

#include <array>
#include <optional>
#include <utility>

using namespace Qt::StringLiterals;
using Link = QcHandbookEntry::Link;
using Section = QcHandbookEntry::Section;
using QcHandbookOverviewPages::orderedLinks;
using QcHandbookOverviewPages::tablePage;

namespace {

// Blocks the tables list: the visible ones a player gets as an item (internal variants such as a
// lit lamp or a piston head belong to their main block) and the liquids.
bool listed(const QcBlockDefinition &block)
{
    return block.renderLayer != QcRenderLayer::Invisible && (block.hasItem || block.liquid);
}

// A label with the server's rule values ({signal.repeaterStepTicks}) filled in.
QString ruleLabel(const QcHandbookContext &context, const QString &key)
{
    return context.text.substituteRules(context.text.label(key), u"handbook."_s + key);
}

// What a Signal Dust component takes and gives (decision E-333 ff.): power level, delay in ticks,
// range or behaviour.
QString signalNote(const QcHandbookContext &context, const QcSignalDefinition &signal)
{
    const QString power = QString::number(signal.power);
    switch (signal.kind) {
    case QcSignalKind::Dust:
        return ruleLabel(context, u"value.signal.dust"_s).arg(QcSignalDefinition::maxPower);
    case QcSignalKind::Lever:
        return ruleLabel(context, u"value.signal.lever"_s).arg(power);
    case QcSignalKind::Button:
        return ruleLabel(context, u"value.signal.button"_s).arg(power).arg(signal.pulseTicks);
    case QcSignalKind::Plate: {
        const QString by = signal.detectsCarts ? u"Carts"_s : (signal.detectsItems ? u"Items"_s : u"Bodies"_s);
        return ruleLabel(context, u"value.signal.plate"_s + by).arg(power);
    }
    case QcSignalKind::Torch:
        return ruleLabel(context, u"value.signal.torch"_s).arg(power);
    case QcSignalKind::Repeater:
        return ruleLabel(context, u"value.signal.repeater"_s).arg(power);
    case QcSignalKind::Comparator:
        return ruleLabel(context, u"value.signal.comparator"_s);
    case QcSignalKind::Observer:
        return ruleLabel(context, u"value.signal.observer"_s).arg(power);
    case QcSignalKind::Lamp:
        return ruleLabel(context, u"value.signal.lamp"_s).arg(signal.offDelayTicks);
    case QcSignalKind::Opener:
        return ruleLabel(context, u"value.signal.opener"_s);
    case QcSignalKind::Piston:
        return ruleLabel(context, signal.sticky ? u"value.signal.stickyPiston"_s : u"value.signal.piston"_s).arg(signal.pushLimit);
    case QcSignalKind::Hopper:
        return ruleLabel(context, u"value.signal.hopper"_s);
    case QcSignalKind::Dispenser:
        return ruleLabel(context, signal.usesItems ? u"value.signal.dispenser"_s : u"value.signal.dropper"_s);
    case QcSignalKind::Rail:
        return ruleLabel(context, u"value.signal.rail"_s).arg(signal.railReach);
    case QcSignalKind::Igniter:
        return ruleLabel(context, u"value.signal.igniter"_s);
    case QcSignalKind::Constant:
        return ruleLabel(context, u"value.signal.constant"_s).arg(power);
    case QcSignalKind::Note:
        return ruleLabel(context, u"value.signal.note"_s).arg(QcSignalDefinition::noteCount);
    case QcSignalKind::Target:
        return ruleLabel(context, u"value.signal.target"_s).arg(signal.stuckHitTicks).arg(signal.hitTicks);
    case QcSignalKind::Hook:
        return ruleLabel(context, u"value.signal.hook"_s).arg(power).arg(signal.wireReach);
    case QcSignalKind::Tripwire:
        return ruleLabel(context, u"value.signal.tripwire"_s).arg(signal.pulseTicks);
    case QcSignalKind::Viewers:
        return ruleLabel(context, u"value.signal.viewers"_s).arg(QcSignalDefinition::maxPower);
    case QcSignalKind::None:
    case QcSignalKind::PistonHead:
        break;
    }
    return {};
}

// The role of a component in a circuit: 0 gives power, 1 carries or shapes it, 2 uses it.
int signalRole(QcSignalKind kind)
{
    switch (kind) {
    case QcSignalKind::Lever:
    case QcSignalKind::Button:
    case QcSignalKind::Plate:
    case QcSignalKind::Torch:
    case QcSignalKind::Observer:
    case QcSignalKind::Constant:
    case QcSignalKind::Target:
    case QcSignalKind::Hook:
    case QcSignalKind::Tripwire:
    case QcSignalKind::Viewers:
        return 0;
    case QcSignalKind::Dust:
    case QcSignalKind::Repeater:
    case QcSignalKind::Comparator:
        return 1;
    default:
        return 2;
    }
}

// Every component by its role, sources first; within a role by kind, then by name.
QcHandbookEntry signalTable(const QcHandbookContext &context)
{
    QcHandbookEntry table = tablePage(context, u"signal"_s, u"qtcraft:signal_dust"_s);
    table.text = context.text.substituteRules(table.text, table.id);
    std::array<QList<std::pair<int, Link>>, 3> roles;
    for (const QcBlockDefinition &block : context.content.blocks()) {
        const QcSignalDefinition &signal = block.signal;
        if (!listed(block) || !signal.isComponent() || signal.kind == QcSignalKind::PistonHead) {
            continue;
        }

        QString note = signalNote(context, signal);
        if (!signal.conducts) {
            note += context.text.label(u"value.signal.insulates"_s);
        }
        if (signal.kind == QcSignalKind::Torch && !signal.strongPowersAbove) {
            note += context.text.label(u"value.signal.noStrongAbove"_s);
        }
        roles[static_cast<std::size_t>(signalRole(signal.kind))].append({static_cast<int>(signal.kind), context.links.blockLink(block.name, note)});
    }

    const std::array<QString, 3> titles{u"section.signalSources"_s, u"section.signalWires"_s, u"section.signalSlots"_s};
    for (std::size_t role = 0; role < roles.size(); ++role) {
        if (!roles[role].isEmpty()) {
            table.sections.append({context.text.label(titles[role]), {}, orderedLinks(std::move(roles[role])), {}});
        }
    }
    return table;
}

// Walkers spawn on solid, opaque blocks with a full top face and inside free cells (QcSpawnGround);
// a slab carries them only in its upper and double states. Water creatures spawn in liquids that do
// not burn.
QString spawnNote(const QcHandbookContext &context, const QcBlockDefinition &block)
{
    if (block.liquid) {
        return context.text.label(block.fire.setsAlightSeconds == 0 ? u"value.spawnInWater"_s : u"value.spawnNever"_s);
    }
    if (block.keepsSpawnsAway() || block.rail) {
        return context.text.label(u"value.spawnNever"_s);
    }
    if (!block.solid) {
        return context.text.label(u"value.spawnIn"_s);
    }

    const std::optional<QcBlockId> state = context.content.blockId(block.name);
    const QcSpawnGround::Carrying carrying =
        state ? QcSpawnGround::carryingOf(context.content, context.content.state(*state).type) : QcSpawnGround::Carrying::None;
    switch (carrying) {
    case QcSpawnGround::Carrying::All:
        return context.text.label(u"value.spawnOn"_s);
    case QcSpawnGround::Carrying::Some:
        return context.text.label(u"value.spawnOnUpper"_s);
    case QcSpawnGround::Carrying::None:
        break;
    }
    return context.text.label(u"value.spawnNotOn"_s);
}

QString lightNote(const QcHandbookContext &context, const QcBlockDefinition &block)
{
    const QcHandbookBlockTables::LightBlocking blocking = QcHandbookBlockTables::lightBlocking(context.content, block);
    if (blocking.coversSides) {
        return context.text.label(u"value.lightCoveredSides"_s);
    }
    if (blocking.opacity == 0) {
        return context.text.label(u"value.lightPasses"_s);
    }
    return context.text.label(u"value.lightDimmed"_s).arg(blocking.opacity);
}

// The blocks that are no opaque full cubes: solid ones, the ones bodies pass through and liquids,
// each with how much light it takes away and whether creatures spawn on or in it.
QcHandbookEntry transparentTable(const QcHandbookContext &context)
{
    QcHandbookEntry table = tablePage(context, u"transparent"_s, u"qtcraft:glass"_s);
    std::array<QList<std::pair<int, Link>>, 3> groups;
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (!listed(block) || block.opaque || !block.partOf.isEmpty()) {
            continue;
        }

        const QString note = lightNote(context, block) + u", "_s + spawnNote(context, block);
        const std::size_t group = block.liquid ? 2 : (block.solid ? 0 : 1);
        groups[group].append({0, context.links.blockLink(block.name, note)});
    }

    const std::array<QString, 3> titles{u"section.solidBlocks"_s, u"section.passableBlocks"_s, u"section.liquids"_s};
    for (std::size_t group = 0; group < groups.size(); ++group) {
        if (!groups[group].isEmpty()) {
            table.sections.append({context.text.label(titles[group]), {}, orderedLinks(std::move(groups[group])), {}});
        }
    }
    return table;
}

// Blocks giving light, brightest first.
QcHandbookEntry lightTable(const QcHandbookContext &context)
{
    QcHandbookEntry table = tablePage(context, u"light"_s, u"qtcraft:torch"_s);
    QList<std::pair<int, Link>> sources;
    for (const QcBlockDefinition &block : context.content.blocks()) {
        // Lit variants without an item of their own (a lit lamp) have pages of their own, too.
        if (block.renderLayer != QcRenderLayer::Invisible && block.lightLevel > 0) {
            const QString note = context.text.label(u"unit.lightLevel"_s).arg(block.lightLevel);
            sources.append({block.lightLevel, context.links.blockLink(block.name, note)});
        }
    }
    table.sections.append({context.text.label(u"section.lightSources"_s), {}, orderedLinks(std::move(sources), true), {}});
    return table;
}

// The resistance as a number; a derived one says where it comes from (a liquid, else the hardness).
QString blastNote(const QcHandbookText &text, const QcBlockDefinition &block, double resistance)
{
    if (block.blast.resistance >= 0.0) {
        return text.number(resistance);
    }
    return text.label(block.liquid ? u"value.blastLiquid"_s : u"value.blastFromHardness"_s).arg(text.number(resistance));
}

// Blast resistance of every block (decision E-600): the ones explosions never break, then the others
// strongest first; resistances derived from the hardness say so.
QcHandbookEntry blastTable(const QcHandbookContext &context)
{
    const QcHandbookText &text = context.text;
    QcHandbookEntry table = tablePage(context, u"blast"_s, u"qtcraft:explosive"_s);
    QList<std::pair<double, Link>> unbreakable;
    QList<std::pair<double, Link>> resisting;
    for (const QcBlockDefinition &block : context.content.blocks()) {
        if (!listed(block)) {
            continue;
        }

        const double resistance = block.blast.effectiveResistance(block.hardness, block.liquid);
        if (resistance >= QcBlastDefinition::unbreakable) {
            unbreakable.append({0.0, context.links.blockLink(block.name)});
            continue;
        }
        resisting.append({resistance, context.links.blockLink(block.name, blastNote(text, block, resistance))});
    }

    if (!unbreakable.isEmpty()) {
        table.sections.append({text.label(u"section.blastProof"_s), {}, orderedLinks(std::move(unbreakable)), {}});
    }
    table.sections.append({text.label(u"section.blastResistance"_s), {}, orderedLinks(std::move(resisting), true), {}});
    return table;
}

} // namespace

namespace QcHandbookBlockTables {

LightBlocking lightBlocking(const QcContentRegistry &content, const QcBlockDefinition &block)
{
    bool partial = false;
    bool coversSides = false;
    if (const std::optional<QcBlockId> id = content.blockId(block.name)) {
        const QcBlockStateTable &states = content.stateTable();
        const quint32 type = content.state(*id).type;
        for (int index = 0; index < states.stateCount(type); ++index) {
            const QcBlockState &state = states.state(states.firstState(type) + static_cast<quint32>(index));
            partial = partial || !state.fullCube;
            coversSides = coversSides || state.lightBlockedFaces != 0;
        }
    }
    return {coversSides, partial ? 0 : block.lightOpacity};
}

QList<QcHandbookEntry> build(const QcHandbookContext &context)
{
    return {signalTable(context), transparentTable(context), lightTable(context), blastTable(context)};
}

} // namespace QcHandbookBlockTables
