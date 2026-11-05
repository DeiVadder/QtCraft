// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

// Payload of the dimension messages (QcDimensionMessages.h, decision E-1002): the client's part of
// the dimension's definition, every field checked against the parser's limits.

#include "common/protocol/QcProtocolCodec.h"

#include <cmath>

using namespace Qt::StringLiterals;

namespace QcProtocolCodec {

namespace {

constexpr quint8 flagSkyLight = 1;
constexpr quint8 flagDayCycle = 2;
constexpr quint8 flagWeather = 4;
constexpr quint8 flagCompassesSpin = 8;
constexpr quint8 flagFog = 16;
constexpr quint8 knownFlags = 31;

// The largest fog colour, 0xRRGGBB.
constexpr quint32 maxColour = 0xFFFFFF;

quint8 flagsOf(const QcDimensionDefinition &dimension)
{
    return static_cast<quint8>((dimension.skyLight ? flagSkyLight : 0) | (dimension.dayCycle ? flagDayCycle : 0)
                               | (dimension.weather ? flagWeather : 0) | (dimension.compassesSpin ? flagCompassesSpin : 0)
                               | (dimension.fogColour ? flagFog : 0));
}

bool plausible(const QcDimensionDefinition &dimension)
{
    using Definition = QcDimensionDefinition;
    const bool named = !dimension.name.isEmpty() && dimension.name.size() <= Definition::maxNameLength
                       && dimension.translationKey.size() <= Definition::maxTranslationKey;
    // Written as a negated range check so that NaN fails it too.
    const bool scaled = dimension.coordinateScale >= 1.0 / Definition::maxCoordinateScale && dimension.coordinateScale <= Definition::maxCoordinateScale;
    return named && scaled && dimension.ceiling >= 0 && dimension.ceiling <= 255 && dimension.ambientLight >= 0 && dimension.ambientLight <= 15
           && (!dimension.fogColour || *dimension.fogColour <= maxColour);
}

} // namespace

void encode(QDataStream &stream, const QcMsg::DimensionChange &m)
{
    const QcDimensionDefinition &dimension = m.dimension;
    stream << dimension.name << dimension.translationKey << static_cast<quint8>(dimension.ceiling) << dimension.coordinateScale
           << static_cast<quint8>(dimension.sky) << static_cast<quint8>(dimension.ambientLight) << flagsOf(dimension)
           << dimension.fogColour.value_or(0);
}

[[nodiscard]] QcResult<QcMessage> decode(Tag<QcMsg::DimensionChange>, QDataStream &stream)
{
    QcMsg::DimensionChange m;
    QcDimensionDefinition &dimension = m.dimension;
    quint8 ceiling = 0;
    quint8 sky = 0;
    quint8 ambient = 0;
    quint8 flags = 0;
    quint32 fog = 0;
    stream >> dimension.name >> dimension.translationKey >> ceiling >> dimension.coordinateScale >> sky >> ambient >> flags >> fog;
    if (sky > static_cast<quint8>(QcDimensionDefinition::Sky::End) || (flags & ~knownFlags) != 0) {
        return qcError(u"Invalid dimension"_s);
    }
    dimension.ceiling = ceiling;
    dimension.sky = static_cast<QcDimensionDefinition::Sky>(sky);
    dimension.ambientLight = ambient;
    dimension.skyLight = (flags & flagSkyLight) != 0;
    dimension.dayCycle = (flags & flagDayCycle) != 0;
    dimension.weather = (flags & flagWeather) != 0;
    dimension.compassesSpin = (flags & flagCompassesSpin) != 0;
    dimension.fogColour = (flags & flagFog) != 0 ? std::optional<quint32>(fog) : std::nullopt;
    if (!plausible(dimension)) {
        return qcError(u"Invalid dimension"_s);
    }
    return m;
}

} // namespace QcProtocolCodec
