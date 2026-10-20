#include "clientcore/settings/QcUiScale.h"

#include <algorithm>
#include <array>
#include <cmath>

using namespace Qt::StringLiterals;

namespace QcUiScale {

namespace {

struct StepInfo
{
    Step step;
    // Percent; 0 for Auto.
    int percent;
};

constexpr std::array<StepInfo, 7> steps{{
    {Step::Auto, 0},
    {Step::Percent75, 75},
    {Step::Percent100, 100},
    {Step::Percent125, 125},
    {Step::Percent150, 150},
    {Step::Percent175, 175},
    {Step::Percent200, 200},
}};

int percentOf(Step step)
{
    const auto *found = std::ranges::find(steps, step, &StepInfo::step);
    return found != steps.end() ? found->percent : 100;
}

// Rounded down to multiples of 1 / `parts`, with a little room for values like 1.9999999.
double floorTo(double value, double parts)
{
    return std::floor(value * parts + 1e-6) / parts;
}

bool usable(QSizeF size)
{
    return std::isfinite(size.width()) && std::isfinite(size.height()) && size.width() > 0.0 && size.height() > 0.0;
}

} // namespace

QString name(Step step)
{
    return step == Step::Auto ? u"auto"_s : QString::number(percentOf(step));
}

std::optional<Step> fromName(const QString &text)
{
    for (const StepInfo &info : steps) {
        if (text == name(info.step)) {
            return info.step;
        }
    }
    return std::nullopt;
}

std::optional<double> fixedFactor(Step step)
{
    if (step == Step::Auto) {
        return std::nullopt;
    }
    return percentOf(step) / 100.0;
}

double autoFactor(QSizeF window)
{
    if (!usable(window)) {
        return 1.0;
    }

    const double bySize = std::min(window.width() / autoReferenceSize.width(), window.height() / autoReferenceSize.height());
    return std::clamp(floorTo(bySize, 4.0), 1.0, maxFactor);
}

double requestedFactor(Step step, QSizeF window)
{
    return fixedFactor(step).value_or(autoFactor(window));
}

double fittedFactor(double requested, QSizeF window, QSizeF designSize)
{
    if (!std::isfinite(requested) || requested <= 0.0) {
        return 1.0;
    }
    if (!usable(window) || !usable(designSize)) {
        return std::min(requested, 1.0);
    }

    const double fits = floorTo(std::min(window.width() / designSize.width(), window.height() / designSize.height()), 8.0);
    return std::min(requested, std::max(1.0, fits));
}

} // namespace QcUiScale
