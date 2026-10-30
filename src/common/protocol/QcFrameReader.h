// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include <optional>

/*! \brief Framing of the protocol on a byte stream (TCP, decision E-251): every frame is a
    big-endian quint32 length followed by that many bytes; a frame of length 0 only keeps the
    connection alive.

    The reader collects arbitrary fragments and hands out whole frames; a length beyond the limit
    breaks the stream for good (the peer is broken or hostile). */
class QcFrameReader
{
public:
    /*! The size of the length prefix. */
    static constexpr qsizetype headerBytes = 4;

    explicit QcFrameReader(qint64 maxFrameBytes);

    /*! Returns the length prefix for a frame of \a size bytes. */
    static QByteArray header(qsizetype size);

    /*! Takes \a bytes, more of the stream. Returns false once a frame announced more than the
        limit. */
    bool append(QByteArrayView bytes);
    /*! Returns the next complete frame (empty for a keep-alive), or nothing until more bytes
        arrive. */
    std::optional<QByteArray> next();
    /*! Returns whether a frame announced more than the limit; the stream is broken for good. */
    bool failed() const { return m_failed; }
    /*! Bytes received but not yet handed out. */
    qsizetype buffered() const { return m_buffer.size() - m_offset; }

private:
    QByteArray m_buffer;
    qsizetype m_offset = 0;
    qint64 m_maxFrameBytes = 0;
    bool m_failed = false;
};
