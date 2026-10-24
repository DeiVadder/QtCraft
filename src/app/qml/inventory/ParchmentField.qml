import QtQuick
import QtCraft

// A field of parchment (decision E-884): the background of the enchanting table's offers and the
// anvil's name field, raised by the blocky bevel of the controls; lighter while hovered, dark and
// sunken while it cannot be used.
Rectangle {
    id: field

    property bool active: true
    property bool hovered: false

    color: !active ? Theme.parchmentDisabled : hovered ? Theme.parchmentHover : Theme.parchment
    radius: Ui.radius

    Bevel {
        sunken: !field.active
    }
}
