#pragma once

#include <QDataStream>

// Bounds for counts read from untrusted streams (network, save files), so corrupt input cannot
// trigger huge allocations.
namespace QcStreamLimits {

// Strings and byte arrays are bounded by the frame size (QcProtocol::maxFrameBytes); QDataStream
// reads them incrementally, so an announced length beyond the frame fails without allocating it.
inline constexpr quint32 maxDefinitions = 1u << 16;

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
