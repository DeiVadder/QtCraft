#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>

// Reads typed fields of one untrusted JSON object. The first problem is kept as `error`, prefixed
// with `context`; later reads return their fallback and add nothing.
struct QcJsonFieldReader
{
    const QJsonObject &json;
    QString context;
    QString error;

    bool ok() const { return error.isEmpty(); }
    void fail(const QString &message);
    // Takes over the error of a reader for a nested object.
    void adopt(const QcJsonFieldReader &nested);

    QString string(const QString &key, const QString &fallback = {});
    bool boolean(const QString &key, bool fallback);
    double number(const QString &key, double fallback, double low, double high);
    int integer(const QString &key, int fallback, int low, int high);
    // A content name; unqualified names get `packNamespace`.
    QString name(const QString &key, const QString &packNamespace, const QString &fallback = {});
    // A colour written as "#rrggbb", as 0xRRGGBB; none without the key (and after a failure).
    [[nodiscard]] std::optional<quint32> colour(const QString &key);

    // Reads the optional object at `key` with a nested reader (context "<context> <key>") and adopts
    // its error. `read` is called as read(const QJsonObject &object, QcJsonFieldReader &nested).
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
