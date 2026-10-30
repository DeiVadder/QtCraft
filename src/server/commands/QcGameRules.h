// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "common/world/QcWorldInfo.h"

#include <QStringView>

#include <span>

/*! \brief The game rules of a world (QcWorldInfo, world.json "gameRules") as /gamerule and the
    admin channel's settings see them (decision E-848): a switch or a number from 0 to \c {highest},
    each with its group, its default and what it does.

    \c {group} and \c {description} are English texts marked with
    QT_TRANSLATE_NOOP("QcGameRules", ...). New rules are one line in all(). */
namespace QcGameRules {

/*! The translation context of the rules' texts. */
inline constexpr char context[] = "QcGameRules";

/*! \brief One game rule. */
struct Rule
{
    /*! The name /gamerule and world.json use. */
    QStringView name;
    /*! The group the admin channel shows it in. */
    const char *group = nullptr;
    /*! What the rule does, one sentence. */
    const char *description = nullptr;
    /*! The member of QcWorldInfo that holds the value: a switch has \c {flag}, a number
        \c {number} (exactly one is set). \qcshared */
    bool QcWorldInfo::*flag = nullptr;
    int QcWorldInfo::*number = nullptr;
    /*! The highest value of a number (1 for a switch). */
    int highest = 1;
    /*! The value of a new world. */
    int defaultValue = 1;
};

/*! Returns every rule. */
std::span<const Rule> all();
/*! Returns the rule named \a name, case sensitive as /gamerule takes it; nullptr when there is
    none. */
const Rule *find(QStringView name);
/*! Returns the value of \a rule in \a info (a switch as 0 or 1). */
qint64 value(const Rule &rule, const QcWorldInfo &info);

} // namespace QcGameRules
