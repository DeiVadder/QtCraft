// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "clientcore/input/QcGamepadState.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

void QcGamepadState::setAxis(QcGamepadAxis axis, float value)
{
    const float low = isTrigger(axis) ? 0.0f : -1.0f;
    m_axes[static_cast<std::size_t>(axis)] = std::isnan(value) ? 0.0f : std::clamp(value, low, 1.0f);
}

namespace {

// In the order of the enums.
const QStringList &buttonNames()
{
    static const QStringList names{u"south"_s,     u"east"_s,         u"west"_s,          u"north"_s,
                                   u"back"_s,      u"guide"_s,        u"start"_s,         u"leftStick"_s,
                                   u"rightStick"_s, u"leftShoulder"_s, u"rightShoulder"_s, u"dpadUp"_s,
                                   u"dpadDown"_s,  u"dpadLeft"_s,     u"dpadRight"_s};
    return names;
}

const QStringList &axisNames()
{
    static const QStringList names{u"leftX"_s, u"leftY"_s, u"rightX"_s, u"rightY"_s, u"leftTrigger"_s, u"rightTrigger"_s};
    return names;
}

const QStringList &typeNames()
{
    static const QStringList names{u"generic"_s, u"xbox"_s, u"playstation"_s, u"switchPro"_s};
    return names;
}

template<typename Enum>
std::optional<Enum> fromName(const QStringList &names, const QString &name)
{
    const qsizetype index = names.indexOf(name);
    return index < 0 ? std::nullopt : std::optional<Enum>(Enum(index));
}

} // namespace

QString QcGamepadNames::button(QcGamepadButton button)
{
    return buttonNames().value(static_cast<qsizetype>(button));
}

QString QcGamepadNames::axis(QcGamepadAxis axis)
{
    return axisNames().value(static_cast<qsizetype>(axis));
}

QString QcGamepadNames::type(QcGamepadType type)
{
    return typeNames().value(static_cast<qsizetype>(type));
}

std::optional<QcGamepadButton> QcGamepadNames::buttonFromName(const QString &name)
{
    return fromName<QcGamepadButton>(buttonNames(), name);
}

std::optional<QcGamepadAxis> QcGamepadNames::axisFromName(const QString &name)
{
    return fromName<QcGamepadAxis>(axisNames(), name);
}

std::optional<QcGamepadType> QcGamepadNames::typeFromName(const QString &name)
{
    return fromName<QcGamepadType>(typeNames(), name);
}
