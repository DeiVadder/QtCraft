#include "server/storage/QcChunkCodec.h"

#include "common/protocol/QcProtocol.h"
#include "common/protocol/QcStreamLimits.h"
#include "server/storage/QcSaveFile.h"

#include <QDataStream>
#include <QIODevice>
#include <QHash>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace {

// Uncompressed chunks are far smaller; more is damaged data.
constexpr qsizetype maxRawBytes = qsizetype{64} * 1024 * 1024;

} // namespace

namespace QcChunkCodec {

QByteArray encode(const QcChunk &chunk, const QcContentRegistry &content)
{
    // A palette local to the chunk; its sections refer to indices into it.
    QList<QString> names;
    QHash<QcBlockId, QcBlockId> localIds;
    QcChunk copy(chunk.pos());
    for (int s = 0; s < QcWorld::sectionCount; ++s) {
        // The copy is compacted, so a section that grew during play is saved without unused entries
        // (and the chunk's names list only blocks that are present).
        QcChunkSection &section = copy.section(s);
        section = chunk.section(s);
        section.compact();
        QList<QcBlockId> palette;
        for (const QcBlockId id : section.palette()) {
            if (!localIds.contains(id)) {
                localIds.insert(id, static_cast<QcBlockId>(names.size()));
                names.append(content.stateName(id));
            }
            palette.append(localIds.value(id));
        }
        [[maybe_unused]] const bool assigned = section.replacePalette(std::move(palette));
        Q_ASSERT(assigned);
    }

    QByteArray raw;
    QDataStream stream(&raw, QIODevice::WriteOnly);
    stream.setVersion(QcSaveFile::streamVersion);
    stream << static_cast<quint32>(names.size());
    for (const QString &name : std::as_const(names)) {
        stream << name;
    }
    QcProtocol::writeChunk(stream, copy);
    return qCompress(raw, 1);
}

[[nodiscard]] QcResult<std::shared_ptr<QcChunk>> decode(const QByteArray &blob, QcChunkPos chunk, const QcContentRegistry &content,
                                          QcLoadWarnings &warnings)
{
    // qUncompress reads the expected size from the first four bytes; a damaged one could ask for
    // gigabytes.
    if (blob.size() < 4 || qFromBigEndian<quint32>(blob.constData()) > static_cast<quint32>(maxRawBytes)) {
        return qcError(u"damaged chunk data"_s);
    }
    const QByteArray raw = qUncompress(blob);
    if (raw.isEmpty()) {
        return qcError(u"damaged chunk data"_s);
    }

    QDataStream stream(raw);
    stream.setVersion(QcSaveFile::streamVersion);
    const std::optional<quint32> nameCountRead = QcStreamLimits::readCount(stream, QcStreamLimits::maxDefinitions);
    if (!nameCountRead) {
        return qcError(u"damaged block palette"_s);
    }
    const quint32 nameCount = *nameCountRead;
    QList<QcBlockId> runtimeIds;
    runtimeIds.reserve(nameCount);
    for (quint32 i = 0; i < nameCount; ++i) {
        QString name;
        stream >> name;
        const QcContentRegistry::ParsedState parsed = content.parseState(name);
        const std::optional<QcBlockId> id = parsed.id;
        const QStringList &problems = parsed.problems;
        if (!id) {
            warnings.addOnce(u"block:"_s + name, u"unknown block '%1' replaced by air (in this world reported only here)"_s.arg(name));
        }
        for (const QString &problem : std::as_const(problems)) {
            warnings.addOnce(u"state:"_s + problem, u"%1, default used"_s.arg(problem));
        }
        runtimeIds.append(id.value_or(qcAirBlockId));
    }

    QcResult<std::shared_ptr<QcChunk>> read = QcProtocol::readChunk(stream);
    if (!read) {
        return qcError(read.error());
    }
    if ((*read)->pos() != chunk || !stream.atEnd() || stream.status() != QDataStream::Ok) {
        return qcError(u"damaged chunk data (position)"_s);
    }
    for (int s = 0; s < QcWorld::sectionCount; ++s) {
        QcChunkSection &section = (*read)->section(s);
        QList<QcBlockId> palette;
        for (const QcBlockId local : section.palette()) {
            if (static_cast<qsizetype>(local) >= runtimeIds.size()) {
                return qcError(u"damaged chunk data (block index)"_s);
            }
            palette.append(runtimeIds[static_cast<qsizetype>(local)]);
        }
        if (!section.replacePalette(std::move(palette))) {
            return qcError(u"damaged chunk data (section)"_s);
        }
        section.compact();
    }
    return read;
}

} // namespace QcChunkCodec
