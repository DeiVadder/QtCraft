#include "QcWeatherParser.h"

#include "QcJsonFieldReader.h"

using namespace Qt::StringLiterals;

namespace QcWeatherParser {

[[nodiscard]] QcResult<QcWeatherDefinition> parseWeather(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"weather"_s, {}};
    QcWeatherDefinition weather;
    reader.nestedObject(u"lightning"_s, [&weather](const QJsonObject &, QcJsonFieldReader &nested) {
        QcWeatherDefinition::Lightning &lightning = weather.lightning;
        lightning.meanSeconds = nested.number(u"meanSeconds"_s, lightning.meanSeconds, 0.5, 3600.0);
        lightning.damage = nested.number(u"damage"_s, lightning.damage, 0.0, 100.0);
        lightning.radius = nested.integer(u"radius"_s, lightning.radius, 0, 8);
        lightning.burnSeconds = nested.integer(u"burnSeconds"_s, lightning.burnSeconds, 0, 60);
        // A handful at most: a strike is a spark, not a fire of its own size.
        lightning.sparks = nested.integer(u"sparks"_s, lightning.sparks, 0, 8);
    });
    reader.nestedObject(u"snow"_s, [&weather, &packNamespace](const QJsonObject &, QcJsonFieldReader &nested) {
        QcWeatherDefinition::Snow &snow = weather.snow;
        snow.layer = nested.name(u"layer"_s, packNamespace, snow.layer);
        snow.columnsPerTick = nested.integer(u"columnsPerTick"_s, snow.columnsPerTick, 0, 64);
    });
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return weather;
}

} // namespace QcWeatherParser
