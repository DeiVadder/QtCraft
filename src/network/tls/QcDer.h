// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QByteArrayList>
#include <QDateTime>

/*! \brief The few DER (ASN.1) encodings a self-signed certificate needs (decision E-872; ITU-T
    X.690): the structure only, the key and its signature come from QcTlsKey.

    Each function returns one complete element. */
namespace QcDer {

/*! Object identifiers, already encoded (the content of an OBJECT IDENTIFIER). */
inline constexpr char commonName[] = "\x55\x04\x03";

/*! Returns the element with \a tag and \a content: tag, length and content. */
[[nodiscard]] QByteArray element(quint8 tag, const QByteArray &content);
/*! Returns a SEQUENCE of the encoded \a elements. */
[[nodiscard]] QByteArray sequence(const QByteArrayList &elements);
/*! Returns a SET of the encoded \a elements. */
[[nodiscard]] QByteArray set(const QByteArrayList &elements);
/*! Returns a non-negative INTEGER from its \a bigEndian bytes (a serial number). */
[[nodiscard]] QByteArray integer(const QByteArray &bigEndian);
/*! Returns the INTEGER \a value. */
[[nodiscard]] QByteArray integer(quint8 value);
/*! Returns an OBJECT IDENTIFIER with the \a encoded content (commonName and the like). */
[[nodiscard]] QByteArray objectIdentifier(const QByteArray &encoded);
/*! Returns a BIT STRING of the whole \a bytes. */
[[nodiscard]] QByteArray bitString(const QByteArray &bytes);
/*! Returns a UTF8String of \a text. */
[[nodiscard]] QByteArray utf8String(const QString &text);
/*! Returns \a when as UTCTime up to 2049, as GeneralizedTime from 2050 on (RFC 5280, 4.1.2.5). */
[[nodiscard]] QByteArray time(const QDateTime &when);
/*! Returns the context-specific constructed tag [\a number] around the element \a inner. */
[[nodiscard]] QByteArray explicitTag(int number, const QByteArray &inner);
/*! Returns \a der as PEM text: base64 in lines of 64 characters between BEGIN and END lines with
    \a label. */
[[nodiscard]] QByteArray pem(const QByteArray &der, const QByteArray &label);

} // namespace QcDer
