#include "QcProceduralTexture.h"

#include "QcTextureDetail.h"
#include "QcTextureEmblem.h"
#include "QcTextureSynthesizer.h"

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
        alpha = int(QStringView(text).mid(7, 2).toUInt(&ok, 16));
        if (!ok) {
            return std::nullopt;
        }
    }
    return QColor(int((rgb >> 16) & 0xFF), int((rgb >> 8) & 0xFF), int(rgb & 0xFF), alpha);
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
    spec.seed = quint32(definition.value(u"seed"_s).toInteger(0));
    QString emblemProblem;
    if (spec.pattern == QcTextureSpec::Pattern::Emblem && !QcTextureEmblem::read(definition, spec, emblemProblem)) {
        return qcError(emblemProblem);
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
