#include "clientcore/settings/QcRenderOptions.h"

#include <QSettings>

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<const char *, 2> modeNames{"classic", "realistic"};
constexpr std::array<const char *, 3> qualityNames{"low", "medium", "high"};

// The index of `name` among `names`, if it is one of them.
template <std::size_t N>
std::optional<std::size_t> indexOf(const std::array<const char *, N> &names, const QString &name)
{
    const auto found = std::ranges::find_if(names, [&name](const char *candidate) { return name == QLatin1StringView(candidate); });
    return found == names.end() ? std::nullopt : std::optional(static_cast<std::size_t>(found - names.begin()));
}

} // namespace

int QcRenderOptions::bloomLevels() const
{
    if (!realistic()) {
        return 0;
    }
    switch (quality) {
    case Quality::Low:
        return 0;
    case Quality::Medium:
        return mediumBloomLevels;
    case Quality::High:
        return highBloomLevels;
    }
    return 0;
}

QString QcRenderOptions::modeName(Mode mode)
{
    return QString::fromLatin1(modeNames[static_cast<std::size_t>(mode)]);
}

std::optional<QcRenderOptions::Mode> QcRenderOptions::modeFromName(const QString &name)
{
    const std::optional<std::size_t> index = indexOf(modeNames, name);
    return index ? std::optional(static_cast<Mode>(*index)) : std::nullopt;
}

QString QcRenderOptions::qualityName(Quality quality)
{
    return QString::fromLatin1(qualityNames[static_cast<std::size_t>(quality)]);
}

std::optional<QcRenderOptions::Quality> QcRenderOptions::qualityFromName(const QString &name)
{
    const std::optional<std::size_t> index = indexOf(qualityNames, name);
    return index ? std::optional(static_cast<Quality>(*index)) : std::nullopt;
}

bool QcRenderOptions::isExposure(double exposure)
{
    return std::isfinite(exposure) && exposure >= minExposure && exposure <= maxExposure;
}

QcRenderOptions QcRenderOptions::load(const QSettings &file)
{
    QcRenderOptions options;
    options.mode = modeFromName(file.value(u"render/mode"_s).toString()).value_or(Mode::Classic);
    options.quality = qualityFromName(file.value(u"render/quality"_s).toString()).value_or(Quality::Medium);
    bool ok = false;
    const double exposure = file.value(u"render/exposure"_s).toDouble(&ok);
    options.exposure = ok && isExposure(exposure) ? exposure : defaultExposure;
    return options;
}

void QcRenderOptions::save(QSettings &file) const
{
    file.setValue(u"render/mode"_s, modeName(mode));
    file.setValue(u"render/quality"_s, qualityName(quality));
    file.setValue(u"render/exposure"_s, exposure);
}
