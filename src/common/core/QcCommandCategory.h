#pragma once

#include <QLatin1StringView>
#include <QStringView>
#include <QtGlobal>

#include <array>
#include <optional>

// The group a command belongs to (decision E-911): /help lists the commands by group and pages
// through a group (QcCommandHelp holds its texts), the command list of "Manage server" shows them
// under the group's title (QcAdminCommandList). Each group has an English word /help takes.
enum class QcCommandCategory : quint8 {
    General,
    Players,
    Items,
    Creatures,
    Building,
    World,
    Finding,
    Server,
    // Macros of the content and C++ extensions that name no group.
    Other,
};

namespace QcCommandCategories {

inline constexpr qsizetype count = 9;

// Every group in the order /help lists them.
const std::array<QcCommandCategory, count> &all();
// The word /help takes for the group ("building").
QLatin1StringView id(QcCommandCategory category);
// The group of the word `word` (case-insensitive); std::nullopt for other words.
[[nodiscard]] std::optional<QcCommandCategory> fromId(QStringView word);
// The group a byte of the protocol names; std::nullopt beyond the last.
[[nodiscard]] std::optional<QcCommandCategory> fromWire(quint8 value);

} // namespace QcCommandCategories
