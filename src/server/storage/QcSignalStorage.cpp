#include "QcSignalStorage.h"

#include "QcSaveFile.h"
#include "QcStreamLimits.h"
#include "QcWorldConstants.h"

#include <QDir>
#include <QFile>

#include <algorithm>
#include <cstdlib>

using namespace Qt::StringLiterals;

namespace {

constexpr QByteArrayView magic("QCSG");
constexpr quint32 maxDelay = 1u << 20;

QString pathIn(const QString &directory)
{
    return QDir(directory).filePath(u"signals.qce"_s);
}

void writePos(QDataStream &stream, QcBlockPos pos)
{
    stream << qint32(pos.x) << qint32(pos.y) << qint32(pos.z);
}

bool readPos(QDataStream &stream, QcBlockPos &pos)
{
    qint32 x = 0;
    qint32 y = 0;
    qint32 z = 0;
    stream >> x >> y >> z;
    pos = {x, y, z};
    return stream.status() == QDataStream::Ok && pos.isInsideWorldHeight() && std::abs(x) <= QcWorld::maxHorizontalBlock
           && std::abs(z) <= QcWorld::maxHorizontalBlock;
}

template<typename Read>
bool readList(QDataStream &stream, Read read)
{
    quint32 count = 0;
    if (!QcStreamLimits::readCount(stream, QcSignalStorage::maxEntries, count)) {
        return false;
    }
    for (quint32 i = 0; i < count; ++i) {
        if (!read()) {
            return false;
        }
    }
    return true;
}

// The count of a list as written: never more than a reader accepts (the rest is left out).
quint32 cappedCount(qsizetype size)
{
    return quint32(std::min<qsizetype>(size, QcSignalStorage::maxEntries));
}

quint32 ticksLeft(quint64 due, quint64 now)
{
    return quint32(std::min<quint64>(due - std::min(due, now), maxDelay));
}

void writeSchedule(QDataStream &stream, const QcCircuitState &circuits, quint64 now)
{
    const QList<QcSignalSchedule::Entry> entries = circuits.schedule.entries();
    const quint32 count = cappedCount(entries.size());
    stream << count;
    for (quint32 i = 0; i < count; ++i) {
        writePos(stream, entries[i].pos);
        stream << ticksLeft(entries[i].due, now) << quint8(entries[i].priority);
    }
}

void writePositions(QDataStream &stream, const QList<QcBlockPos> &positions)
{
    const quint32 count = cappedCount(positions.size());
    stream << count;
    for (quint32 i = 0; i < count; ++i) {
        writePos(stream, positions[i]);
    }
}

// Positions with a tick each, written as ticks left (`towards` the tick) or ticks since it.
void writeTimed(QDataStream &stream, const QHash<QcBlockPos, quint64> &ticks, quint64 now, bool left)
{
    const quint32 count = cappedCount(ticks.size());
    stream << count;
    quint32 written = 0;
    for (auto it = ticks.constBegin(); it != ticks.constEnd() && written < count; ++it, ++written) {
        writePos(stream, it.key());
        stream << (left ? ticksLeft(it.value(), now) : quint32(std::min<quint64>(now - std::min(now, it.value()), maxDelay)));
    }
}

QByteArray payloadOf(const QcCircuitState &circuits, quint64 now)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);

    writeSchedule(stream, circuits, now);
    QList<QcBlockPos> pending;
    for (const QcCircuitState::Change &change : circuits.changes) {
        pending.append(change.pos);
    }
    for (const QList<QcBlockPos> &positions : circuits.waiting) {
        pending.append(positions);
    }
    for (const QList<QcBlockPos> &positions : circuits.waitingOn) {
        pending.append(positions);
    }
    writePositions(stream, pending);
    writeTimed(stream, circuits.burntOutUntil, now, true);
    writePositions(stream, QList<QcBlockPos>(circuits.poweredOpeners.begin(), circuits.poweredOpeners.end()));
    writeTimed(stream, circuits.extending, now, false);
    return payload;
}

bool readPayload(QDataStream &stream, QcCircuitState &circuits, quint64 now)
{
    QcBlockPos pos;
    const bool scheduled = readList(stream, [&] {
        quint32 delay = 0;
        quint8 priority = 0;
        const bool placed = readPos(stream, pos);
        stream >> delay >> priority;
        const bool ok = placed && stream.status() == QDataStream::Ok && delay <= maxDelay && priority < QcSignalSchedule::priorityCount;
        if (ok) {
            circuits.schedule.add(pos, now + delay, QcSignalSchedule::Priority(priority));
        }
        return ok;
    });
    const bool changes = scheduled && readList(stream, [&] {
        // The block is looked at again as if it had just been placed, once its chunk is loaded.
        const bool ok = readPos(stream, pos);
        circuits.waiting[pos.chunk()].append(pos);
        return ok;
    });
    const bool burnt = changes && readList(stream, [&] {
        quint32 left = 0;
        const bool ok = readPos(stream, pos);
        stream >> left;
        circuits.burntOutUntil.insert(pos, now + std::min(left, maxDelay));
        return ok && stream.status() == QDataStream::Ok;
    });
    const bool openers = burnt && readList(stream, [&] {
        const bool ok = readPos(stream, pos);
        circuits.poweredOpeners.insert(pos);
        return ok;
    });
    return openers && readList(stream, [&] {
        quint32 since = 0;
        const bool ok = readPos(stream, pos);
        stream >> since;
        circuits.extending.insert(pos, now - std::min<quint64>({now, since, maxDelay}));
        return ok && stream.status() == QDataStream::Ok;
    });
}

} // namespace

namespace QcSignalStorage {

QcStatus write(const QString &directory, const QcCircuitState &circuits, quint64 now)
{
    const QString path = pathIn(directory);
    const bool quiet = circuits.schedule.size() == 0 && circuits.changes.isEmpty() && circuits.waiting.isEmpty() && circuits.waitingOn.isEmpty()
                       && circuits.burntOutUntil.isEmpty() && circuits.poweredOpeners.isEmpty() && circuits.extending.isEmpty();
    if (quiet) {
        if (QFile::exists(path) && !QFile::remove(path)) {
            return qcError(u"Cannot remove %1"_s.arg(path));
        }
        return {};
    }
    if (!QDir().mkpath(directory)) {
        return qcError(u"Cannot create %1"_s.arg(directory));
    }
    return QcSaveFile::write(path, magic, payloadOf(circuits, now));
}

QcStatus read(const QString &directory, QcCircuitState &circuits, quint64 now)
{
    circuits.clear();
    const QString path = pathIn(directory);
    if (!QFile::exists(path)) {
        return {};
    }

    const QcResult<QByteArray> payload = QcSaveFile::read(path, magic);
    if (!payload) {
        return qcError(payload.error());
    }

    QDataStream stream(*payload);
    stream.setVersion(QcSaveFile::streamVersion);
    if (!readPayload(stream, circuits, now) || !stream.atEnd()) {
        circuits.clear();
        return qcError(u"%1 is damaged"_s.arg(path));
    }
    return {};
}

} // namespace QcSignalStorage
