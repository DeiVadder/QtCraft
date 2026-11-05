// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/world/QcGeneratorVersionMap.h"

#include "common/protocol/QcStreamLimits.h"
#include "common/world/QcGeneratorVersion.h"

#include <QDataStream>
#include <QIODevice>

#include <utility>

using namespace Qt::StringLiterals;

std::optional<int> QcGeneratorVersionMap::versionAt(QcChunkPos chunk) const
{
    for (auto it = m_versions.cbegin(); it != m_versions.cend(); ++it) {
        if (it->contains(chunk)) {
            return it.key();
        }
    }
    return std::nullopt;
}

qsizetype QcGeneratorVersionMap::mark(const QcChunkSet &chunks, int version)
{
    Q_ASSERT(version >= 1 && version <= QcGeneratorVersion::maxStored);
    QcChunkSet added = chunks;
    for (const QcChunkSet &named : std::as_const(m_versions)) {
        added = added.without(named);
    }
    if (added.isEmpty()) {
        return 0;
    }
    const qsizetype count = added.size();
    m_versions[version].unite(added);
    return count;
}

QcChunkSet QcGeneratorVersionMap::allChunks() const
{
    QcChunkSet all;
    for (const QcChunkSet &chunks : m_versions) {
        all.unite(chunks);
    }
    return all;
}

QByteArray QcGeneratorVersionMap::bytes() const
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QcChunkSet::streamVersion);
    stream << static_cast<quint32>(m_versions.size());
    for (auto it = m_versions.cbegin(); it != m_versions.cend(); ++it) {
        const QByteArray chunks = it->bytes();
        stream << static_cast<quint16>(it.key()) << static_cast<quint32>(chunks.size());
        stream.writeRawData(chunks.constData(), static_cast<int>(chunks.size()));
    }
    return payload;
}

QcResult<QcGeneratorVersionMap> QcGeneratorVersionMap::parse(QByteArrayView bytes)
{
    const QByteArray copy = bytes.toByteArray();
    QDataStream stream(copy);
    stream.setVersion(QcChunkSet::streamVersion);
    const std::optional<quint32> count = QcStreamLimits::readCount(stream, QcGeneratorVersion::maxStored);
    if (!count) {
        return qcError(u"cut short"_s);
    }

    QcGeneratorVersionMap map;
    int previous = 0;
    for (quint32 i = 0; i < *count; ++i) {
        quint16 version = 0;
        quint32 size = 0;
        stream >> version >> size;
        const qint64 left = copy.size() - stream.device()->pos();
        if (stream.status() != QDataStream::Ok || std::cmp_greater(size, left)) {
            return qcError(u"cut short"_s);
        }
        if (std::cmp_less_equal(version, previous) || std::cmp_greater(version, QcGeneratorVersion::maxStored)) {
            return qcError(u"version %1 out of range or order"_s.arg(version));
        }
        QByteArray raw(static_cast<qsizetype>(size), '\0');
        stream.readRawData(raw.data(), static_cast<int>(size));
        QcResult<QcChunkSet> chunks = QcChunkSet::parse(raw);
        if (!chunks) {
            return qcError(u"version %1: %2"_s.arg(version).arg(chunks.error()));
        }
        if (chunks->isEmpty() || chunks->intersects(map.allChunks())) {
            return qcError(u"version %1 names no chunk or a chunk of another version"_s.arg(version));
        }
        map.m_versions.insert(version, *chunks);
        previous = version;
    }
    if (!stream.atEnd()) {
        return qcError(u"trailing bytes"_s);
    }
    return map;
}
