#include "QcFrameReader.h"

#include <QtEndian>

#include <utility>

QcFrameReader::QcFrameReader(qint64 maxFrameBytes)
    : m_maxFrameBytes(maxFrameBytes)
{
}

QByteArray QcFrameReader::header(qsizetype size)
{
    QByteArray bytes(headerBytes, Qt::Uninitialized);
    qToBigEndian(quint32(size), bytes.data());
    return bytes;
}

bool QcFrameReader::append(QByteArrayView bytes)
{
    if (m_failed) {
        return false;
    }

    // Handed-out frames are dropped from the front once they make up half of the buffer.
    if (m_offset > 0 && m_offset >= m_buffer.size() / 2) {
        m_buffer.remove(0, m_offset);
        m_offset = 0;
    }
    m_buffer.append(bytes);

    // The announced length is checked as soon as it is complete, before its bytes arrive.
    if (buffered() >= headerBytes) {
        const quint32 length = qFromBigEndian<quint32>(m_buffer.constData() + m_offset);
        m_failed = std::cmp_greater(length, m_maxFrameBytes);
    }
    return !m_failed;
}

std::optional<QByteArray> QcFrameReader::next()
{
    if (m_failed || buffered() < headerBytes) {
        return std::nullopt;
    }

    const quint32 length = qFromBigEndian<quint32>(m_buffer.constData() + m_offset);
    if (std::cmp_greater(length, m_maxFrameBytes)) {
        m_failed = true;
        return std::nullopt;
    }
    if (buffered() < headerBytes + qsizetype(length)) {
        return std::nullopt;
    }

    QByteArray frame = m_buffer.mid(m_offset + headerBytes, qsizetype(length));
    m_offset += headerBytes + qsizetype(length);
    if (m_offset == m_buffer.size()) {
        m_buffer.clear();
        m_offset = 0;
    }
    return frame;
}
