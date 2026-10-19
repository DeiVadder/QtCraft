// Chunk encoding of the protocol (QcProtocol::writeChunk/readChunk), shared with persistence:
// per section a palette plus run-length encoded indices (decision E-016).

#include "common/protocol/QcProtocol.h"

#include "common/protocol/QcStreamLimits.h"

#include <QDataStream>
#include <QVarLengthArray>
#include <QtEndian>

using namespace Qt::StringLiterals;

namespace {

// Runs of a section kept on the stack while encoding or decoding; more go to the heap.
constexpr qsizetype runBufferRuns = 512;

void writeSection(QDataStream &stream, const QcChunkSection &section)
{
    stream << quint32(section.palette().size());
    for (const QcBlockId id : section.palette()) {
        stream << id;
    }

    if (section.isUniform()) {
        return;
    }

    // Run-length encoded palette indices: terrain sections consist mostly of long runs. Decoding
    // is bounded by the section size, unlike a general-purpose decompressor. The runs (length and
    // index, two 16-bit values each in the stream's byte order) go out as one block of raw data.
    const QList<quint16> &indices = section.indices();
    const bool bigEndian = stream.byteOrder() == QDataStream::BigEndian;
    QVarLengthArray<quint16, 2 * runBufferRuns> runs;
    for (qsizetype i = 0; i < indices.size();) {
        qsizetype end = i + 1;
        while (end < indices.size() && indices[end] == indices[i] && end - i < 0xFFFF) {
            ++end;
        }
        for (const quint16 value : {quint16(end - i), indices[i]}) {
            runs.append(bigEndian ? qToBigEndian(value) : qToLittleEndian(value));
        }
        i = end;
    }

    stream << quint32(runs.size() / 2);
    stream.writeRawData(reinterpret_cast<const char *>(runs.constData()), int(runs.size() * qsizetype(sizeof(quint16))));
}

bool readSection(QDataStream &stream, QcChunkSection &section)
{
    const std::optional<quint32> paletteSizeRead = QcStreamLimits::readCount(stream, QcWorld::blocksPerSection);
    if (!paletteSizeRead || *paletteSizeRead == 0) {
        return false;
    }
    const quint32 paletteSize = *paletteSizeRead;

    QList<QcBlockId> palette(paletteSize);
    for (QcBlockId &id : palette) {
        stream >> id;
    }

    QList<quint16> indices;
    if (paletteSize > 1) {
        const std::optional<quint32> runCountRead = QcStreamLimits::readCount(stream, QcWorld::blocksPerSection);
        if (!runCountRead) {
            return false;
        }
        const quint32 runCount = *runCountRead;

        QVarLengthArray<quint16, 2 * runBufferRuns> runs(qsizetype(runCount) * 2);
        const int bytes = int(runs.size() * qsizetype(sizeof(quint16)));
        if (stream.readRawData(reinterpret_cast<char *>(runs.data()), bytes) != bytes) {
            return false;
        }

        const bool bigEndian = stream.byteOrder() == QDataStream::BigEndian;
        indices.reserve(QcWorld::blocksPerSection);
        for (qsizetype run = 0; run < runs.size(); run += 2) {
            const quint16 length = bigEndian ? qFromBigEndian(runs[run]) : qFromLittleEndian(runs[run]);
            const quint16 index = bigEndian ? qFromBigEndian(runs[run + 1]) : qFromLittleEndian(runs[run + 1]);
            if (length == 0 || indices.size() + length > QcWorld::blocksPerSection) {
                return false;
            }
            indices.insert(indices.size(), length, index);
        }
    }

    return stream.status() == QDataStream::Ok && section.assign(std::move(palette), std::move(indices));
}

} // namespace

namespace QcProtocol {

void writeChunk(QDataStream &stream, const QcChunk &chunk)
{
    stream << qint32(chunk.pos().x) << qint32(chunk.pos().z);
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        writeSection(stream, chunk.section(i));
    }
}

[[nodiscard]] QcResult<std::shared_ptr<QcChunk>> readChunk(QDataStream &stream)
{
    qint32 x = 0;
    qint32 z = 0;
    stream >> x >> z;

    auto chunk = std::make_shared<QcChunk>(QcChunkPos{x, z});
    for (int i = 0; i < QcWorld::sectionCount; ++i) {
        if (!readSection(stream, chunk->section(i))) {
            return qcError(u"Corrupt chunk data in section %1"_s.arg(i));
        }
    }
    return chunk;
}

} // namespace QcProtocol
