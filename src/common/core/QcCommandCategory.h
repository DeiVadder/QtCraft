// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLatin1StringView>
#include <QStringView>
#include <QtGlobal>

#include <array>
#include <optional>

/*! \brief The group a command belongs to (decision E-911): /help lists the commands by group and
    pages through a group (QcCommandHelp holds its texts), the command list of "Manage server" shows
    them under the group's title (QcAdminCommandList).

    Each group has an English word /help takes. */
enum class QcCommandCategory : quint8 {
    /*! General commands ("general"). */
    General,
    /*! Players ("players"). */
    Players,
    /*! Items and health ("items"). */
    Items,
    /*! Creatures ("creatures"). */
    Creatures,
    /*! Building ("building"). */
    Building,
    /*! The world and its rules ("world"). */
    World,
    /*! Finding places ("finding"). */
    Finding,
    /*! The server ("server"). */
    Server,
    /*! Macros of the content and C++ extensions that name no group. */
    Other,
};

/*! \brief The groups of commands as a list, by their words and by their byte in the protocol. */
namespace QcCommandCategories {

/*! The number of groups. */
inline constexpr qsizetype count = 9;

/*! Every group in the order /help lists them. */
const std::array<QcCommandCategory, count> &all();
/*! Returns the word /help takes for the group \a category ("building"). */
QLatin1StringView id(QcCommandCategory category);
/*! Returns the group of the word \a word (case-insensitive); std::nullopt for other words. */
[[nodiscard]] std::optional<QcCommandCategory> fromId(QStringView word);
/*! Returns the group the protocol byte \a value names; std::nullopt beyond the last. */
[[nodiscard]] std::optional<QcCommandCategory> fromWire(quint8 value);

} // namespace QcCommandCategories
