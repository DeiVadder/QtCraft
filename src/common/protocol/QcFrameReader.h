#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

// Framing of the protocol on a byte stream (TCP, decision E-251): every frame is a big-endian
// quint32 length followed by that many bytes; a frame of length 0 only keeps the connection alive.
// The reader collects arbitrary fragments and hands out whole frames; a length beyond the limit
// breaks the stream for good (the peer is broken or hostile).
class QcFrameReader
{
public:
    static constexpr qsizetype headerBytes = 4;

    explicit QcFrameReader(qint64 maxFrameBytes);

    // The length prefix for a frame of `size` bytes.
    static QByteArray header(qsizetype size);

    // Takes more bytes of the stream; false once a frame announced more than the limit.
    bool append(QByteArrayView bytes);
    // The next complete frame (empty for a keep-alive), or nothing until more bytes arrive.
    std::optional<QByteArray> next();
    bool failed() const { return m_failed; }
    // Bytes received but not yet handed out.
    qsizetype buffered() const { return m_buffer.size() - m_offset; }

private:
    QByteArray m_buffer;
    qsizetype m_offset = 0;
    qint64 m_maxFrameBytes = 0;
    bool m_failed = false;
};
