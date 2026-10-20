#include "common/world/QcSignText.h"

#include "common/core/QcNameText.h"

#include <algorithm>

namespace QcSignText {

Lines cleaned(const Lines &lines, int count, int lineLength)
{
    Lines result;
    const int kept = std::clamp(count, 0, maxLines);
    for (int line = 0; line < kept; ++line) {
        result[std::size_t(line)] = QcNameText::clean(lines[std::size_t(line)], std::clamp(lineLength, 0, maxLineLength));
    }
    return result;
}

bool isEmpty(const Lines &lines)
{
    return std::ranges::all_of(lines, [](const QString &line) { return line.isEmpty(); });
}

bool fitsWire(const Lines &lines)
{
    return std::ranges::all_of(lines, [](const QString &line) { return line.size() <= maxLineUnits; });
}

} // namespace QcSignText
