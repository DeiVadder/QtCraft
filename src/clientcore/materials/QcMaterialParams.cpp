#include "clientcore/materials/QcMaterialParams.h"

#include "clientcore/debug/QcClientLogging.h"

#include <QJsonValue>
#include <QVariant>

#include <array>
#include <cmath>
#include <limits>
#include <utility>

using namespace Qt::StringLiterals;

QcMaterialParams QcMaterialParams::flat()
{
    QcMaterialParams params;
    params.smoothness = 0.0f;
    params.bump = 0.0f;
    params.porosity = 0.0f;
    return params;
}

std::optional<QcMaterialParams::Metal> QcMaterialParams::metalFromName(const QString &name)
{
    static constexpr std::array<std::pair<const char *, Metal>, 10> names{{
        {"none", Metal::None},
        {"iron", Metal::Iron},
        {"gold", Metal::Gold},
        {"aluminium", Metal::Aluminium},
        {"chrome", Metal::Chrome},
        {"copper", Metal::Copper},
        {"lead", Metal::Lead},
        {"platinum", Metal::Platinum},
        {"silver", Metal::Silver},
        {"albedo", Metal::Albedo},
    }};
    for (const auto &[key, metal] : names) {
        if (name == QLatin1StringView(key)) {
            return metal;
        }
    }
    return std::nullopt;
}

namespace {

// A finite number in [low, high] of `object[key]`, else unset; a value that is there but not such
// a number is reported (decision E-688).
std::optional<double> numberIn(const QJsonObject &object, QLatin1StringView key, double low, double high)
{
    if (!object.contains(key)) {
        return std::nullopt;
    }
    const QJsonValue value = object.value(key);
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || number < low || number > high) {
        qCWarning(lcMaterials).noquote() << u"Material: %1 %2 ignored, expected a number from %3 to %4"_s.arg(key, value.toVariant().toString())
                                                .arg(low)
                                                .arg(high);
        return std::nullopt;
    }
    return number;
}

} // namespace

QcMaterialParams QcMaterialParams::withOverrides(QcMaterialParams base, const QJsonObject &material)
{
    if (const std::optional<double> value = numberIn(material, "smoothness"_L1, 0.0, 1.0)) {
        base.smoothness = static_cast<float>(*value);
    }
    if (const std::optional<double> value = numberIn(material, "reflectance"_L1, 0.0, 229.0)) {
        base.reflectance = static_cast<int>(std::lround(*value));
    }
    if (material.contains("metal"_L1)) {
        const QString name = material.value("metal"_L1).toString();
        if (const std::optional<Metal> metal = metalFromName(name)) {
            base.metal = *metal;
        } else {
            qCWarning(lcMaterials).noquote() << u"Material: metal \"%1\" ignored, not one of the labPBR metals"_s.arg(name);
        }
    }
    if (const std::optional<double> value = numberIn(material, "bump"_L1, 0.0, 4.0)) {
        base.bump = static_cast<float>(*value);
    }
    if (const std::optional<double> value = numberIn(material, "porosity"_L1, 0.0, 1.0)) {
        base.porosity = static_cast<float>(*value);
    }
    if (const std::optional<double> value = numberIn(material, "emission"_L1, 0.0, 1.0)) {
        base.emission = static_cast<float>(*value);
    }
    if (const std::optional<double> value = numberIn(material, "emissionTexels"_L1, 0.0, 1.0)) {
        base.emissionTexels = static_cast<float>(*value);
    }
    return base;
}
