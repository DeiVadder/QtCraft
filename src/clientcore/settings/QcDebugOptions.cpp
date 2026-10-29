// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/settings/QcDebugOptions.h"

#include <QSettings>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

constexpr std::array<const char *, 3> levelNames{"off", "compact", "full"};
constexpr std::array<const char *, QcDebugOptions::visualCount> visualIds{"chunkBorders", "hitboxes", "lightValues", "mobGoals",
                                                                          "remeshFlash"};

} // namespace

QString QcDebugOptions::levelName(Level level)
{
    return QString::fromLatin1(levelNames[static_cast<std::size_t>(level)]);
}

std::optional<QcDebugOptions::Level> QcDebugOptions::levelFromName(const QString &name)
{
    const auto found = std::ranges::find_if(levelNames, [&name](const char *candidate) { return name == QLatin1StringView(candidate); });
    return found == levelNames.end() ? std::nullopt : std::optional(static_cast<Level>(found - levelNames.begin()));
}

QcDebugOptions::Level QcDebugOptions::nextLevel(Level level)
{
    return static_cast<Level>((static_cast<int>(level) + 1) % static_cast<int>(levelNames.size()));
}

QString QcDebugOptions::visualId(Visual visual)
{
    return QString::fromLatin1(visualIds[static_cast<std::size_t>(visual)]);
}

std::optional<QcDebugOptions::Visual> QcDebugOptions::visualFromId(const QString &id)
{
    const auto found = std::ranges::find_if(visualIds, [&id](const char *candidate) { return id == QLatin1StringView(candidate); });
    return found == visualIds.end() ? std::nullopt : std::optional(static_cast<Visual>(found - visualIds.begin()));
}

bool QcDebugOptions::anyVisual() const
{
    return std::ranges::any_of(visuals, [](bool on) { return on; });
}

QcDebugOptions QcDebugOptions::load(const QSettings &file)
{
    QcDebugOptions options;
    if (const std::optional<Level> level = levelFromName(file.value(u"debug/level"_s).toString())) {
        options.level = *level;
    } else if (file.contains(u"showDebug"_s)) {
        options.level = file.value(u"showDebug"_s).toBool() ? Level::Compact : Level::Off;
    }

    for (int visual = 0; visual < visualCount; ++visual) {
        // Only "true" switches one on; anything else in the file leaves it off.
        options.visuals[static_cast<std::size_t>(visual)] = file.value(u"debug/"_s + visualId(static_cast<Visual>(visual))).toString() == u"true"_s;
    }
    return options;
}

void QcDebugOptions::save(QSettings &file) const
{
    file.setValue(u"debug/level"_s, levelName(level));
    for (int visual = 0; visual < visualCount; ++visual) {
        file.setValue(u"debug/"_s + visualId(static_cast<Visual>(visual)), visuals[static_cast<std::size_t>(visual)]);
    }
    // Replaced by debug/level.
    file.remove(u"showDebug"_s);
}
