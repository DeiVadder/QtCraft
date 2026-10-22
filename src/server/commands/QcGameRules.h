#pragma once

#include "common/world/QcWorldInfo.h"

#include <QStringView>

#include <span>

// The game rules of a world (QcWorldInfo, world.json "gameRules") as /gamerule and the admin
// channel's settings see them (decision E-848): a switch or a number from 0 to `highest`, each with
// its group, its default and what it does. `group` and `description` are English texts marked with
// QT_TRANSLATE_NOOP("QcGameRules", ...). New rules are one line in all().
namespace QcGameRules {

inline constexpr char context[] = "QcGameRules";

struct Rule
{
    QStringView name;
    const char *group = nullptr;
    const char *description = nullptr;
    // A switch has `flag`, a number `number` (exactly one is set).
    bool QcWorldInfo::*flag = nullptr;
    int QcWorldInfo::*number = nullptr;
    int highest = 1;
    int defaultValue = 1;
};

std::span<const Rule> all();
// By name, case sensitive as /gamerule takes it; nullptr when there is none.
const Rule *find(QStringView name);
// The rule's value in `info` (a switch as 0 or 1).
qint64 value(const Rule &rule, const QcWorldInfo &info);

} // namespace QcGameRules
