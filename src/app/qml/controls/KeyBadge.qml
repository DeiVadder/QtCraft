// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtCraft

/*! \brief A key or pad button as a small plate next to a label or in a button (decision E-845): "H"
    next to "Handbook" instead of "Handbook (H)".

    Read with the label it belongs to. */
Rectangle {
    id: badge

    /*! The key's name on the plate ("H", "Esc"). */
    property alias text: label.text

    implicitWidth: Math.max(Ui.size.badge, label.implicitWidth + 2 * Ui.space.xs)
    implicitHeight: Ui.size.badge
    radius: Ui.radius
    color: Theme.field
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    Bevel {
        sunken: true
    }

    Label {
        id: label

        anchors.centerIn: parent
        font.pixelSize: Ui.type.badge
        font.bold: true
        color: Theme.textSecondary
        textFormat: Text.PlainText
    }
}
