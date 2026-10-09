#include "QcControlArguments.h"

#include <QJsonValue>

#include <algorithm>
#include <array>

using namespace Qt::StringLiterals;

std::optional<QString> QcControlArguments::typeError(const QJsonObject &request)
{
    for (const char *key :
         {"yaw", "pitch", "forward", "strafe", "ticks", "radius", "x", "y", "z", "timeoutPolls", "slot", "from", "to", "maxTicks", "entity", "count"}) {
        const QJsonValue value = request.value(QLatin1StringView(key));
        if (!value.isUndefined() && !value.isDouble()) {
            return u"'%1' must be a number"_s.arg(QLatin1StringView(key));
        }
    }

    for (const char *key : {"jump", "sneak", "sprint", "all", "clear"}) {
        const QJsonValue value = request.value(QLatin1StringView(key));
        if (!value.isUndefined() && !value.isBool()) {
            return u"'%1' must be true or false"_s.arg(QLatin1StringView(key));
        }
    }
    return std::nullopt;
}

int QcControlArguments::intField(const QJsonObject &request, const QString &key, int fallback)
{
    return request.value(key).toInt(fallback);
}

int QcControlArguments::ticks(const QJsonObject &request, int fallback)
{
    return std::clamp(intField(request, u"ticks"_s, fallback), 0, maxCommandTicks);
}

QcBlockPos QcControlArguments::block(const QJsonObject &request)
{
    return {intField(request, u"x"_s, 0), intField(request, u"y"_s, 0), intField(request, u"z"_s, 0)};
}

std::optional<QcBlockFace> QcControlArguments::face(const QJsonObject &request)
{
    const QString name = request.value(u"face"_s).toString(u"up"_s);
    for (const QcBlockFace face : qcAllBlockFaces) {
        if (faceName(face) == name) {
            return face;
        }
    }
    return std::nullopt;
}

QString QcControlArguments::faceName(QcBlockFace face)
{
    static const std::array<QString, qcBlockFaceCount> names = {u"east"_s, u"west"_s, u"up"_s, u"down"_s, u"south"_s, u"north"_s};
    return names[std::size_t(face)];
}
