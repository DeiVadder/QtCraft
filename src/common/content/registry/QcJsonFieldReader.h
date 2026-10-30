// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

/*! \brief Reads typed fields of one untrusted JSON object.

    The first problem is kept as \c {error}, prefixed with \c {context}; later problems do not
    replace it. A read that fails returns its fallback; reads after a failure still return the
    values they find. */
struct QcJsonFieldReader
{
    /*! The object read; the reader does not own it. */
    const QJsonObject &json;
    /*! Where the object comes from, e.g. "advancement 'qtcraft:root'"; prefixes the error. */
    QString context;
    /*! The first problem, prefixed with the context; empty while all reads succeeded. */
    QString error;

    /*! Returns whether no read has failed yet. */
    bool ok() const { return error.isEmpty(); }
    /*! Records \a message, prefixed with the context, as the error unless there already is one. */
    void fail(const QString &message);
    /*! Takes over the error of the reader \a nested for a nested object, if it has one. */
    void adopt(const QcJsonFieldReader &nested);

    /*! Returns the string at \a key; \a fallback when the key is missing or not a string (the
        latter fails). */
    QString string(const QString &key, const QString &fallback = {});
    /*! Returns the boolean at \a key; \a fallback when the key is missing or not a boolean (the
        latter fails). */
    bool boolean(const QString &key, bool fallback);
    /*! Returns the number at \a key; \a fallback when the key is missing or not a number from
        \a low to \a high (the latter fails). */
    double number(const QString &key, double fallback, double low, double high);
    /*! Returns the whole number at \a key; \a fallback when the key is missing or not a whole
        number from \a low to \a high (the latter fails). */
    int integer(const QString &key, int fallback, int low, int high);
    /*! Returns the content name at \a key (\a fallback when missing); unqualified names get
        \a packNamespace. An invalid name fails and gives an empty string. */
    QString name(const QString &key, const QString &packNamespace, const QString &fallback = {});
    /*! Returns the colour at \a key written as "#rrggbb", as 0xRRGGBB; none when the key is
        missing or empty, or when the text is no such colour (which fails). */
    [[nodiscard]] std::optional<quint32> colour(const QString &key);

    /*! Reads the optional object at \a key with a nested reader (context "<context> <key>") and
        adopts its error. \a read is called as read(const QJsonObject &object, QcJsonFieldReader
        &nested). */
    template<typename Read>
    void nestedObject(const QString &key, Read read)
    {
        const QJsonValue value = json.value(key);
        if (!value.isUndefined() && !value.isObject()) {
            fail(QStringLiteral("'%1' must be an object").arg(key));
            return;
        }

        if (value.isObject()) {
            const QJsonObject object = value.toObject();
            QcJsonFieldReader nested{object, context + QLatin1Char(' ') + key, {}};
            read(object, nested);
            adopt(nested);
        }
    }
};
