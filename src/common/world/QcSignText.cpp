#include "common/world/QcSignText.h"

#include "common/core/QcNameText.h"

#include <algorithm>
#include <utility>

namespace QcSignText {

namespace {

// The default ink, also black dye's: the dark brown of unpainted sign text.
constexpr Ink defaultInk{40, 28, 16};

// The ink of each dye colour (decision E-830). Vanilla darkens the dye's colour to 40 %; these are
// picked so that each stands off birch planks (#d2bd88, the lightest wood) at a WCAG contrast of at
// least 2:1 and oak and spruce by more, while still reading as its colour: the light dyes become
// darker tones (yellow ochre, white a light and light gray a middle gray), the others stay
// saturated.
constexpr std::array<std::pair<QStringView, Ink>, 16> inks{{
    {u"white", {132, 132, 128}},
    {u"orange", {200, 86, 10}},
    {u"magenta", {170, 34, 160}},
    {u"light_blue", {48, 116, 190}},
    {u"yellow", {150, 120, 0}},
    {u"lime", {70, 140, 16}},
    {u"pink", {206, 74, 128}},
    {u"gray", {64, 64, 64}},
    {u"light_gray", {104, 104, 100}},
    {u"cyan", {18, 128, 140}},
    {u"purple", {104, 32, 166}},
    {u"blue", {30, 52, 168}},
    {u"brown", {96, 54, 22}},
    {u"green", {44, 96, 20}},
    {u"red", {176, 22, 22}},
    {u"black", defaultInk},
}};

} // namespace

Lines cleaned(const Lines &lines, int count, int lineLength)
{
    Lines result;
    const int kept = std::clamp(count, 0, maxLines);
    for (int line = 0; line < kept; ++line) {
        result[static_cast<std::size_t>(line)] = QcNameText::clean(lines[static_cast<std::size_t>(line)], std::clamp(lineLength, 0, maxLineLength));
    }
    return result;
}

bool isEmpty(const Lines &lines)
{
    return std::ranges::all_of(lines, [](const QString &line) { return line.isEmpty(); });
}

bool isBlank(const Text &text)
{
    return isEmpty(text.lines) && text.color.isEmpty();
}

std::optional<QString> colorOfDye(QStringView dye)
{
    if (dye == u"black") {
        return QString();
    }
    const auto found = std::ranges::find(inks, dye, &std::pair<QStringView, Ink>::first);
    return found != inks.end() ? std::optional(found->first.toString()) : std::nullopt;
}

Ink inkOf(QStringView color)
{
    const auto found = std::ranges::find(inks, color, &std::pair<QStringView, Ink>::first);
    return found != inks.end() ? found->second : defaultInk;
}

bool fitsWire(const Lines &lines)
{
    return std::ranges::all_of(lines, [](const QString &line) { return line.size() <= maxLineUnits; });
}

} // namespace QcSignText
