// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "common/content/registry/QcJsonFieldReader.h"

#include "common/content/registry/QcContentNames.h"

#include <QJsonValue>

using namespace Qt::StringLiterals;

void QcJsonFieldReader::fail(const QString &message)
{
    if (error.isEmpty()) {
        error = context + u": "_s + message;
    }
}

void QcJsonFieldReader::adopt(const QcJsonFieldReader &nested)
{
    if (!nested.ok()) {
        fail(nested.error);
    }
}

QString QcJsonFieldReader::string(const QString &key, const QString &fallback)
{
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return fallback;
    }
    if (!value.isString()) {
        fail(u"'%1' must be a string"_s.arg(key));
        return fallback;
    }
    return value.toString();
}

bool QcJsonFieldReader::boolean(const QString &key, bool fallback)
{
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return fallback;
    }
    if (!value.isBool()) {
        fail(u"'%1' must be true or false"_s.arg(key));
        return fallback;
    }
    return value.toBool();
}

double QcJsonFieldReader::number(const QString &key, double fallback, double low, double high)
{
    const QJsonValue value = json.value(key);
    if (value.isUndefined()) {
        return fallback;
    }
    if (!value.isDouble() || value.toDouble() < low || value.toDouble() > high) {
        fail(u"'%1' must be a number within %2..%3"_s.arg(key).arg(low).arg(high));
        return fallback;
    }
    return value.toDouble();
}

int QcJsonFieldReader::integer(const QString &key, int fallback, int low, int high)
{
    const double value = number(key, fallback, low, high);
    if (value != static_cast<double>(static_cast<int>(value))) {
        fail(u"'%1' must be an integer"_s.arg(key));
        return fallback;
    }
    return static_cast<int>(value);
}

QString QcJsonFieldReader::name(const QString &key, const QString &packNamespace, const QString &fallback)
{
    QString raw = string(key, fallback);
    if (raw.isEmpty()) {
        return raw;
    }

    const QString qualified = QcContentNames::qualify(raw, packNamespace);
    if (qualified.isEmpty()) {
        fail(u"'%1' is not a valid name: '%2'"_s.arg(key, raw));
    }
    return qualified;
}

std::optional<quint32> QcJsonFieldReader::colour(const QString &key)
{
    const QString text = string(key);
    if (text.isEmpty()) {
        return std::nullopt;
    }
    bool ok = text.size() == 7 && text.startsWith(u'#');
    const quint32 rgb = ok ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
    if (!ok) {
        fail(u"'%1' must be a colour like \"#4e9331\""_s.arg(key));
        return std::nullopt;
    }
    return rgb;
}
