#include "QcChunkCommands.h"

#include "QcCommandArguments.h"
#include "QcServerState.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

const char *const usage = QT_TRANSLATE_NOOP("QcServerMessages", "Usage: /forceload <add|remove> <x> <z> [<x2> <z2>] | remove all | query [<x> <z>]");

QString chunkText(QcChunkPos chunk)
{
    return u"%1, %2"_s.arg(chunk.x).arg(chunk.z);
}

} // namespace

QcChunkCommands::QcChunkCommands(QcServerState &state)
    : m_state(state)
{
}

void QcChunkCommands::addTo(QcCommandRegistry &registry)
{
    registry.addBuiltIn({.name = u"forceload"_s, .minArguments = 1, .usage = usage,
                         .description = QT_TRANSLATE_NOOP("QcServerMessages",
                                                          "Keeps chunks loaded although no player is near (by block coordinates of a "
                                                          "chunk or the corners of an area), lets them go again or lists them."),
                         .run = [this](const QcCommandContext &context) { forceload(context); }});
}

std::optional<QList<QcChunkPos>> QcChunkCommands::area(const QcCommandContext &context, qsizetype first) const
{
    const QStringList &args = context.args;
    const std::optional<QcBlockPos> origin = context.source.origin();
    const auto column = [&](qsizetype index) -> std::optional<QcChunkPos> {
        const std::optional<int> x = QcCommandArguments::blockCoordinate(args.value(index), origin ? std::optional<int>(origin->x) : std::nullopt);
        const std::optional<int> z = QcCommandArguments::blockCoordinate(args.value(index + 1), origin ? std::optional<int>(origin->z) : std::nullopt);
        if (!x || !z) {
            return std::nullopt;
        }
        return QcBlockPos{*x, 0, *z}.chunk();
    };
    const std::optional<QcChunkPos> from = column(first);
    const std::optional<QcChunkPos> to = args.size() > first + 2 ? column(first + 2) : from;
    if (!from || !to || (args.size() != first + 2 && args.size() != first + 4)) {
        context.reply(usage);
        return std::nullopt;
    }
    const int width = std::abs(to->x - from->x) + 1;
    const int depth = std::abs(to->z - from->z) + 1;
    if (qint64(width) * depth > maxArea) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "At most %1 chunks at once"), {QString::number(maxArea)});
        return std::nullopt;
    }
    QList<QcChunkPos> chunks;
    for (int x = std::min(from->x, to->x); x <= std::max(from->x, to->x); ++x) {
        for (int z = std::min(from->z, to->z); z <= std::max(from->z, to->z); ++z) {
            chunks.append({x, z});
        }
    }
    return chunks;
}

void QcChunkCommands::forceload(const QcCommandContext &context)
{
    const QString &what = context.args[0];
    if (what == u"query"_s) {
        query(context);
        return;
    }
    if (what == u"remove"_s && context.args.size() == 2 && context.args[1] == u"all"_s) {
        const qsizetype count = m_state.info().forcedChunks.size();
        m_state.info().forcedChunks.clear();
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 chunks are no longer forced"), {QString::number(count)});
        return;
    }
    if (what != u"add"_s && what != u"remove"_s) {
        context.reply(usage);
        return;
    }
    const std::optional<QList<QcChunkPos>> chunks = area(context, 1);
    if (!chunks) {
        return;
    }
    if (what == u"add"_s) {
        add(context, *chunks);
    } else {
        remove(context, *chunks);
    }
}

void QcChunkCommands::add(const QcCommandContext &context, const QList<QcChunkPos> &chunks)
{
    QList<QcChunkPos> &forced = m_state.info().forcedChunks;
    const qsizetype fresh = std::ranges::count_if(chunks, [&forced](QcChunkPos chunk) { return !forced.contains(chunk); });
    const qint64 limit = m_state.config().limits.forcedChunks;
    if (forced.size() + fresh > limit) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "At most %1 chunks may be forced (forcedChunks); %2 are"),
                      {QString::number(limit), QString::number(forced.size())});
        return;
    }
    for (const QcChunkPos &chunk : chunks) {
        if (!forced.contains(chunk)) {
            forced.append(chunk);
        }
    }
    if (chunks.size() == 1) {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "Chunk %1 is forced"), {chunkText(chunks.constFirst())});
    } else {
        context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 chunks are forced (%2 new)"), {QString::number(chunks.size()), QString::number(fresh)});
    }
}

void QcChunkCommands::remove(const QcCommandContext &context, const QList<QcChunkPos> &chunks)
{
    const qsizetype removed = m_state.info().forcedChunks.removeIf([&chunks](QcChunkPos chunk) { return chunks.contains(chunk); });
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 chunks are no longer forced"), {QString::number(removed)});
}

void QcChunkCommands::query(const QcCommandContext &context)
{
    const QList<QcChunkPos> &forced = m_state.info().forcedChunks;
    if (context.args.size() > 1) {
        const std::optional<QList<QcChunkPos>> chunks = area(context, 1);
        if (!chunks || chunks->size() != 1) {
            return;
        }
        const QcChunkPos chunk = chunks->constFirst();
        context.reply(forced.contains(chunk) ? QT_TRANSLATE_NOOP("QcServerMessages", "Chunk %1 is forced")
                                             : QT_TRANSLATE_NOOP("QcServerMessages", "Chunk %1 is not forced"),
                      {chunkText(chunk)});
        return;
    }
    QStringList shown;
    for (qsizetype i = 0; i < std::min<qsizetype>(forced.size(), 16); ++i) {
        shown.append(chunkText(forced[i]));
    }
    context.reply(QT_TRANSLATE_NOOP("QcServerMessages", "%1 forced chunks: %2"), {QString::number(forced.size()), shown.join(u"; "_s)});
}
