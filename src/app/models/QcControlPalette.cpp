// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/models/QcControlPalette.h"

#include "clientcore/settings/QcUiPalette.h"

#include <QMetaEnum>
#include <QQuickWindow>

#include <cctype>
#include <utility>

namespace QcControlPalette {

namespace {

// The QML name of a palette role: QPalette::ToolTipBase is palette.toolTipBase.
QByteArray propertyName(QPalette::ColorRole role)
{
    QByteArray name = QMetaEnum::fromType<QPalette::ColorRole>().valueToKey(role);
    if (!name.isEmpty()) {
        name[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    }
    return name;
}

// Writes the roles of one colour group of `palette` into a QML colour group (QQuickColorGroup,
// reached through its properties because the type is private).
void writeGroup(QObject &group, const QPalette &palette, QPalette::ColorGroup colorGroup)
{
    for (int role = 0; role < QPalette::NColorRoles; ++role) {
        const auto colorRole = QPalette::ColorRole(role);
        const QByteArray name = propertyName(colorRole);
        if (!name.isEmpty() && colorRole != QPalette::NoRole) {
            group.setProperty(name.constData(), palette.color(colorGroup, colorRole));
        }
    }
}

} // namespace

bool applyTo(QQuickWindow &window)
{
    auto *quickPalette = window.property("palette").value<QObject *>();
    if (!quickPalette) {
        return false;
    }

    const QPalette palette = QcUiPalette::controlPalette();
    writeGroup(*quickPalette, palette, QPalette::Active);
    for (const auto &[name, group] : {std::pair{"active", QPalette::Active}, {"inactive", QPalette::Inactive}, {"disabled", QPalette::Disabled}}) {
        if (auto *colorGroup = quickPalette->property(name).value<QObject *>()) {
            writeGroup(*colorGroup, palette, group);
        }
    }
    return true;
}

} // namespace QcControlPalette
