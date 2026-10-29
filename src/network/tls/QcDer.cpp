// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "network/tls/QcDer.h"

#include <QTimeZone>

namespace QcDer {

namespace {

constexpr quint8 tagInteger = 0x02;
constexpr quint8 tagBitString = 0x03;
constexpr quint8 tagObjectIdentifier = 0x06;
constexpr quint8 tagUtf8String = 0x0C;
constexpr quint8 tagUtcTime = 0x17;
constexpr quint8 tagGeneralizedTime = 0x18;
constexpr quint8 tagSequence = 0x30;
constexpr quint8 tagSet = 0x31;
constexpr quint8 tagContext = 0xA0;

// Short form below 128, otherwise the number of length bytes first.
QByteArray length(qsizetype size)
{
    if (size < 0x80) {
        return QByteArray(1, static_cast<char>(size));
    }
    QByteArray bytes;
    for (qsizetype rest = size; rest > 0; rest >>= 8) {
        bytes.prepend(static_cast<char>(rest & 0xFF));
    }
    bytes.prepend(static_cast<char>(0x80 | bytes.size()));
    return bytes;
}

} // namespace

QByteArray element(quint8 tag, const QByteArray &content)
{
    return QByteArray(1, static_cast<char>(tag)) + length(content.size()) + content;
}

QByteArray sequence(const QByteArrayList &elements)
{
    return element(tagSequence, elements.join());
}

QByteArray set(const QByteArrayList &elements)
{
    return element(tagSet, elements.join());
}

QByteArray integer(const QByteArray &bigEndian)
{
    QByteArray bytes = bigEndian;
    while (bytes.size() > 1 && bytes.front() == '\0') {
        bytes.remove(0, 1);
    }
    if (bytes.isEmpty()) {
        bytes = QByteArray(1, '\0');
    }
    // A set top bit would make the number negative.
    if ((static_cast<quint8>(bytes.front()) & 0x80U) != 0) {
        bytes.prepend('\0');
    }
    return element(tagInteger, bytes);
}

QByteArray integer(quint8 value)
{
    return integer(QByteArray(1, static_cast<char>(value)));
}

QByteArray objectIdentifier(const QByteArray &encoded)
{
    return element(tagObjectIdentifier, encoded);
}

QByteArray bitString(const QByteArray &bytes)
{
    // No unused bits in the last byte.
    return element(tagBitString, QByteArray(1, '\0') + bytes);
}

QByteArray utf8String(const QString &text)
{
    return element(tagUtf8String, text.toUtf8());
}

QByteArray time(const QDateTime &when)
{
    const QDateTime utc = when.toTimeZone(QTimeZone::UTC);
    if (utc.date().year() < 2050) {
        return element(tagUtcTime, utc.toString(QStringLiteral("yyMMddHHmmss'Z'")).toLatin1());
    }
    return element(tagGeneralizedTime, utc.toString(QStringLiteral("yyyyMMddHHmmss'Z'")).toLatin1());
}

QByteArray explicitTag(int number, const QByteArray &inner)
{
    return element(static_cast<quint8>(tagContext | number), inner);
}

QByteArray pem(const QByteArray &der, const QByteArray &label)
{
    const QByteArray base64 = der.toBase64();
    QByteArray text = "-----BEGIN " + label + "-----\n";
    for (qsizetype offset = 0; offset < base64.size(); offset += 64) {
        text += base64.mid(offset, 64) + '\n';
    }
    return text + "-----END " + label + "-----\n";
}

} // namespace QcDer
