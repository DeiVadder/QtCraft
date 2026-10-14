#pragma once

#include <QDataStream>

// Bounds for counts read from untrusted streams (network, save files), so corrupt input cannot
// trigger huge allocations.
namespace QcStreamLimits {

// Strings and byte arrays are bounded by the frame size (QcProtocol::maxFrameBytes); QDataStream
// reads them incrementally, so an announced length beyond the frame fails without allocating it.
inline constexpr quint32 maxDefinitions = 1u << 16;
// Translations of all languages together (decision E-371). A short entry costs about 100 bytes of
// heap but only 16 bytes in a stream, so the frame size alone would let 2 million entries (200 MB per
// copy) through; the base game holds about 2,600.
inline constexpr quint32 maxTranslations = 1u << 18;

// Reads a quint32 count and checks it against `limit`; sets the stream to ReadCorruptData otherwise.
inline bool readCount(QDataStream &stream, quint32 limit, quint32 &count)
{
    stream >> count;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }
    if (count > limit) {
        stream.setStatus(QDataStream::ReadCorruptData);
        return false;
    }
    return true;
}

} // namespace QcStreamLimits
