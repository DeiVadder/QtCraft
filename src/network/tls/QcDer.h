// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QByteArrayList>
#include <QDateTime>

// The few DER (ASN.1) encodings a self-signed certificate needs (decision E-872; ITU-T X.690): the
// structure only, the key and its signature come from QcTlsKey. Each function returns one complete
// element.
namespace QcDer {

// Object identifiers, already encoded (the content of an OBJECT IDENTIFIER).
inline constexpr char commonName[] = "\x55\x04\x03";

// Tag, length and content.
[[nodiscard]] QByteArray element(quint8 tag, const QByteArray &content);
[[nodiscard]] QByteArray sequence(const QByteArrayList &elements);
[[nodiscard]] QByteArray set(const QByteArrayList &elements);
// A non-negative INTEGER from its big-endian bytes (a serial number).
[[nodiscard]] QByteArray integer(const QByteArray &bigEndian);
[[nodiscard]] QByteArray integer(quint8 value);
[[nodiscard]] QByteArray objectIdentifier(const QByteArray &encoded);
// A BIT STRING of whole bytes.
[[nodiscard]] QByteArray bitString(const QByteArray &bytes);
[[nodiscard]] QByteArray utf8String(const QString &text);
// UTCTime up to 2049, GeneralizedTime from 2050 on (RFC 5280, 4.1.2.5).
[[nodiscard]] QByteArray time(const QDateTime &when);
// A context-specific constructed tag [number] around one element.
[[nodiscard]] QByteArray explicitTag(int number, const QByteArray &inner);
// PEM text: base64 in lines of 64 characters between BEGIN and END lines with the label.
[[nodiscard]] QByteArray pem(const QByteArray &der, const QByteArray &label);

} // namespace QcDer
