#include "clientcore/textures/QcProceduralTexture.h"

#include "clientcore/textures/QcTextureDetail.h"
#include "clientcore/textures/QcTextureEmblem.h"
#include "clientcore/textures/QcTextureSynthesizer.h"

#include <QJsonArray>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

// Accepts #RRGGBB and #RRGGBBAA.
[[nodiscard]] std::optional<QColor> parseColor(const QJsonValue &value)
{
    const QString text = value.toString();
    if (!text.startsWith(u'#') || (text.size() != 7 && text.size() != 9)) {
        return std::nullopt;
    }

    bool ok = false;
    const uint rgb = QStringView(text).mid(1, 6).toUInt(&ok, 16);
    if (!ok) {
        return std::nullopt;
    }

    int alpha = 255;
    if (text.size() == 9) {
        alpha = static_cast<int>(QStringView(text).mid(7, 2).toUInt(&ok, 16));
        if (!ok) {
            return std::nullopt;
        }
    }
    return QColor(static_cast<int>((rgb >> 16) & 0xFF), static_cast<int>((rgb >> 8) & 0xFF), static_cast<int>(rgb & 0xFF), alpha);
}

} // namespace

namespace QcProceduralTexture {

[[nodiscard]] QcResult<QcTextureSpec> spec(const QJsonObject &definition)
{
    const std::optional<QcTextureSpec::Pattern> pattern = qcTexturePatternFromName(definition.value(u"pattern"_s).toString());
    const std::optional<QColor> primary = parseColor(definition.value(u"primary"_s));
    const std::optional<QColor> secondary =
        definition.contains(u"secondary"_s) ? parseColor(definition.value(u"secondary"_s)) : primary;
    if (!pattern || !primary || !secondary) {
        return qcError(u"needs a known 'pattern' and colours '#RRGGBB[AA]'"_s);
    }

    QcTextureSpec spec;
    spec.pattern = *pattern;
    spec.primary = *primary;
    spec.secondary = *secondary;
    spec.variation = std::clamp(definition.value(u"variation"_s).toDouble(0.1), 0.0, 1.0);
    spec.seed = static_cast<quint32>(definition.value(u"seed"_s).toInteger(0));
    // Checked by the content parser (QcContentParser::textureTiles); a texture pack's spec is clamped.
    const QJsonArray tiles = definition.value(u"tiles"_s).toArray();
    if (tiles.size() == 2) {
        spec.tilesWide = std::clamp(tiles[0].toInt(1), 1, 4);
        spec.tilesHigh = std::clamp(tiles[1].toInt(1), 1, 4);
    }
    const QJsonArray tile = definition.value(u"tile"_s).toArray();
    if (tile.size() == 2) {
        spec.tile = std::pair{std::clamp(tile[0].toInt(0), 0, spec.tilesWide - 1), std::clamp(tile[1].toInt(0), 0, spec.tilesHigh - 1)};
    }
    if (spec.pattern == QcTextureSpec::Pattern::Emblem || spec.pattern == QcTextureSpec::Pattern::Icon) {
        return QcTextureEmblem::read(definition, std::move(spec));
    }
    return spec;
}

[[nodiscard]] QcResult<QImage> render(const QJsonObject &definition, int resolution)
{
    const QcResult<QcTextureSpec> parsed = spec(definition);
    if (!parsed) {
        return qcError(parsed.error());
    }

    QImage base = QcTextureSynthesizer::synthesize(*parsed);
    if (resolution <= QcTextureSynthesizer::tileSize) {
        return base;
    }

    const QcTextureDetail::Params params =
        QcTextureDetail::withOverrides(QcTextureDetail::forPattern(parsed->pattern), definition.value(u"detail"_s).toObject());
    return QcTextureDetail::enlarge(base, resolution, params, parsed->seed);
}

} // namespace QcProceduralTexture
