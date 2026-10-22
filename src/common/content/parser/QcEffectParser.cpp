#include "common/content/parser/QcEffectParser.h"

#include "common/content/parser/QcContentFields.h"
#include "common/content/parser/QcJsonFieldReader.h"

using namespace Qt::StringLiterals;

namespace {

// "#rrggbb" as 0xRRGGBB; the fallback without the key.
quint32 readColour(const QString &key, quint32 fallback, QcJsonFieldReader &reader)
{
    const QString text = reader.string(key);
    if (text.isEmpty()) {
        return fallback;
    }
    bool ok = text.size() == 7 && text.startsWith(u'#');
    const quint32 rgb = ok ? QStringView(text).mid(1).toUInt(&ok, 16) : 0;
    if (!ok) {
        reader.fail(u"'%1' must be a colour like \"#4e9331\""_s.arg(key));
        return fallback;
    }
    return rgb;
}

} // namespace

namespace QcEffectParser {

QcResult<QcEffectDefinition> parseEffect(const QJsonObject &json, const QString &packNamespace)
{
    QcJsonFieldReader reader{json, u"effect"_s, {}};
    QcEffectDefinition effect;
    effect.name = reader.name(u"name"_s, packNamespace);
    if (effect.name.isEmpty()) {
        return qcError(reader.ok() ? u"effect: 'name' is required"_s : reader.error);
    }

    reader.context = u"effect '%1'"_s.arg(effect.name);
    effect.translationKey = reader.string(u"translation"_s, QcContentFields::translationKey(u"effect"_s, effect.name));
    effect.icon = reader.name(u"icon"_s, packNamespace);
    effect.colour = readColour(u"colour"_s, effect.colour, reader);
    effect.tintsHearts = reader.boolean(u"tintsHearts"_s, effect.tintsHearts);
    effect.damage = reader.number(u"damage"_s, effect.damage, 0.0, 1000.0);
    effect.periodTicks = reader.integer(u"periodTicks"_s, effect.periodTicks, 1, 72000);
    effect.minHealth = reader.number(u"minHealth"_s, effect.minHealth, 0.0, 1000.0);
    effect.speed = reader.number(u"speed"_s, effect.speed, -1.0, 1.0);
    effect.exhaustion = reader.number(u"exhaustion"_s, effect.exhaustion, 0.0, 100.0);
    if (!reader.ok()) {
        return qcError(reader.error);
    }
    return effect;
}

} // namespace QcEffectParser
