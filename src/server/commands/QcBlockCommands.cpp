#include "QcBlockCommands.h"

#include "QcBlockRules.h"
#include "QcBlockUpdateQueue.h"
#include "QcCommandArguments.h"
#include "QcPendingCommands.h"
#include "QcServerBlocks.h"
#include "QcServerState.h"

#include <QScopeGuard>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const char *const fillUsage = QT_TRANSLATE_NOOP("QcServerMessages",
                                                "Usage: /fill <x1> <y1> <z1> <x2> <y2> <z2> <block> [replace <block>] "
                                                "(at most %1 blocks; ~ for coordinates relative to you) in loaded chunks");

QcChunkPos chunkOfColumn(int x, int z)
{
    return QcBlockPos{x, 0, z}.chunk();
}

} // namespace

qint64 QcBlockCommands::BlockArea::volume() const
{
    return (qint64(high.x) - low.x + 1) * (qint64(high.y) - low.y + 1) * (qint64(high.z) - low.z + 1);
}

QcBlockCommands::QcBlockCommands(QcServerState &state, QcServerBlocks &blocks, QcBlockUpdateQueue &updates, QcPendingCommands &pending)
    : m_state(state)
    , m_blocks(blocks)
    , m_updates(updates)
    , m_pending(pending)
    , m_regeneration(state, updates)
{
}

void QcBlockCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"setblock"_s, .minArguments = 4,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /setblock <x> <y> <z> <block>[states]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Sets one block; ~ means your position, air breaks the block."),
                         .run = [this](const QcCommandContext &context) { setBlock(context); }});
    registry.addBuiltIn({.name = u"fill"_s, .minArguments = 7,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /fill <x1> <y1> <z1> <x2> <y2> <z2> <block> [replace <block>]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Fills a box with a block, or replaces only one kind of block in it."),
                         .run = [this](const QcCommandContext &context) { fill(context, false); }});
    registry.addBuiltIn({.name = u"clear"_s, .minArguments = 6,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /clear <x1> <y1> <z1> <x2> <y2> <z2> [block]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages", "Turns a box (or only one kind of block in it) into air."),
                         .run = [this](const QcCommandContext &context) { fill(context, true); }});
    registry.addBuiltIn({.name = u"regenerate"_s,
                         .usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /regenerate [<x> <z> [<x2> <z2>]]"),
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "Generates chunks again as the world type makes them; their changes are lost."),
                         .run = [this](const QcCommandContext &context) { regenerate(context); }});
}

void QcBlockCommands::setBlock(const QcCommandContext &context)
{
    if (const std::optional<BlockArea> area = parseArea(context, 1, context.args[3])) {
        apply(context.source, *area, true);
    }
}

void QcBlockCommands::fill(const QcCommandContext &context, bool clear)
{
    std::optional<BlockArea> area = parseArea(context, 2, clear ? u"air"_s : context.args[6]);
    if (!area || !parseFilter(context, *area, clear)) {
        return;
    }

    if (area->volume() <= confirmFillBlocks) {
        apply(context.source, *area, false);
        return;
    }

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "This changes up to %1 blocks."), {QString::number(area->volume())});
    m_pending.ask(context, [this, area = *area](const QcCommandSource &source) { apply(source, area, false); }, m_state.clock().playerTimeNs());
}

std::optional<QcBlockId> QcBlockCommands::parseBlock(const QcCommandContext &context, const QString &text, QString &name) const
{
    // "name" or "name[property=value,...]"; without states, connections follow the neighbours.
    name = text.section(u'[', 0, 0).contains(u':') ? text : u"qtcraft:"_s + text;
    QStringList problems;
    const std::optional<QcBlockId> block = m_state.content().stateId(name, &problems);
    if (block && !problems.isEmpty()) {
        context.source.replyPlain(problems.join(u"; "_s));
        return std::nullopt;
    }
    if (!block) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Unknown block: %1"), {text});
    }
    return block;
}

std::optional<QcBlockCommands::BlockArea> QcBlockCommands::parseArea(const QcCommandContext &context, qsizetype corners,
                                                                     const QString &blockWord) const
{
    const std::optional<QcBlockPos> origin = context.source.origin();
    const std::optional<QcBlockPos> from = QcCommandArguments::blockPosition(context.args, 0, origin);
    const std::optional<QcBlockPos> to = corners == 2 ? QcCommandArguments::blockPosition(context.args, 3, origin) : from;
    BlockArea area;
    if (from && to) {
        area.from = *from;
        area.low = {std::min(from->x, to->x), std::min(from->y, to->y), std::min(from->z, to->z)};
        area.high = {std::max(from->x, to->x), std::max(from->y, to->y), std::max(from->z, to->z)};
    }

    if (!from || !to || !area.low.isInsideWorldHeight() || !area.high.isInsideWorldHeight() || area.volume() > maxFillBlocks) {
        if (corners == 1) {
            context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /setblock <x> <y> <z> <block>[states]"));
        } else {
            context.reply(fillUsage, {QString::number(maxFillBlocks)});
        }
        return std::nullopt;
    }

    const std::optional<QcBlockId> block = parseBlock(context, blockWord, area.name);
    if (!block) {
        return std::nullopt;
    }
    area.block = *block;
    area.explicitState = area.name.contains(u'[');
    return area;
}

bool QcBlockCommands::parseFilter(const QcCommandContext &context, BlockArea &area, bool clear) const
{
    // /fill … <block> replace <filter>, /clear … <filter>.
    const QStringList &args = context.args;
    const bool valid = clear ? args.size() <= 7 : (args.size() == 7 || (args.size() == 9 && args[7] == u"replace"_s));
    if (!valid) {
        context.reply(clear ? QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /clear <x1> <y1> <z1> <x2> <y2> <z2> [block]") : fillUsage,
                      {QString::number(maxFillBlocks)});
        return false;
    }

    const qsizetype filterIndex = clear ? 6 : 8;
    if (args.size() <= filterIndex) {
        return true;
    }

    QString name;
    area.filter = parseBlock(context, args[filterIndex], name);
    area.filterState = name.contains(u'[');
    return area.filter.has_value();
}

void QcBlockCommands::apply(const QcCommandSource &source, const BlockArea &area, bool single)
{
    const bool machine = m_state.machineRules().machineOf(area.block) != nullptr;

    // The changes and their cascades queue behind the updates of player actions.
    m_updates.setCommandMode(true);
    const auto normalMode = qScopeGuard([this] { m_updates.setCommandMode(false); });

    int changed = 0;
    for (int y = area.low.y; y <= area.high.y; ++y) {
        for (int z = area.low.z; z <= area.high.z; ++z) {
            for (int x = area.low.x; x <= area.high.x; ++x) {
                changed += changeBlock({x, y, z}, area, machine) ? 1 : 0;
            }
        }
    }

    if (changed == 0 && !area.filter) {
        source.reply(machine ? QT_TRANSLATE_NOOP("QcServerMessages", "Nothing set: the chunks are not loaded or there are too many machines")
                             : QT_TRANSLATE_NOOP("QcServerMessages", "Nothing set: the chunks are not loaded"));
    } else if (single) {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Set %1 at %2 %3 %4"),
                     {area.name, QString::number(area.from.x), QString::number(area.from.y), QString::number(area.from.z)});
    } else {
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Filled %1 blocks with %2"), {QString::number(changed), area.name});
    }
}

bool QcBlockCommands::changeBlock(QcBlockPos pos, const BlockArea &area, bool machine)
{
    const std::optional<QcBlockId> current = m_state.world().blockAt(pos);
    if (!current || (machine && !m_state.blockEntities().machine(pos) && m_state.blockEntities().isMachineTableFull())) {
        return false;
    }
    if (area.filter && (area.filterState ? *current != *area.filter : !m_state.content().isSameBlock(*current, *area.filter))) {
        return false;
    }

    if (area.block == qcAirBlockId) {
        m_blocks.breakBlock(pos, false);
        return true;
    }

    // Setting the block makes its machine, chest or sapling timer (QcServerBlocks::setQuietly).
    m_blocks.setAndBroadcast(pos, area.explicitState ? area.block : QcBlockRules::connectedState(m_state.world(), pos, area.block));
    return true;
}

void QcBlockCommands::regenerate(const QcCommandContext &context)
{
    const QStringList &args = context.args;
    const std::optional<QcBlockPos> origin = context.source.origin();
    const auto column = [&](qsizetype first) -> std::optional<QcBlockPos> {
        const QStringList words{args.value(first), u"0"_s, args.value(first + 1)};
        return args.size() >= first + 2 ? QcCommandArguments::blockPosition(words, 0, origin) : origin;
    };
    const std::optional<QcBlockPos> from = column(0);
    const std::optional<QcBlockPos> to = args.size() >= 4 ? column(2) : from;
    if (!from || !to || (!args.isEmpty() && args.size() != 2 && args.size() != 4)) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /regenerate [<x> <z> [<x2> <z2>]]"));
        return;
    }

    const QcChunkPos low = chunkOfColumn(std::min(from->x, to->x), std::min(from->z, to->z));
    const QcChunkPos high = chunkOfColumn(std::max(from->x, to->x), std::max(from->z, to->z));
    if ((qint64(high.x) - low.x + 1) * (qint64(high.z) - low.z + 1) > QcChunkRegeneration::maxChunks) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "At most %1 chunks at once"), {QString::number(QcChunkRegeneration::maxChunks)});
        return;
    }

    QList<QcChunkPos> chunks;
    for (int x = low.x; x <= high.x; ++x) {
        for (int z = low.z; z <= high.z; ++z) {
            chunks.append({x, z});
        }
    }

    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Regenerating %1 chunks from %2 %3 to %4 %5 discards every change in them."),
                  {QString::number(chunks.size()), QString::number(low.x), QString::number(low.z), QString::number(high.x),
                   QString::number(high.z)});
    m_pending.ask(context, [this, chunks](const QcCommandSource &source) {
        const int done = m_regeneration.regenerate(chunks);
        source.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Regenerated %1 chunks (%2 were not loaded and unchanged)"),
                     {QString::number(done), QString::number(chunks.size() - done)});
    }, m_state.clock().playerTimeNs());
}
