import QtQuick
import QtQuick.Layouts
import QtCraft

// The side bar of the menus (docs/UI-Leitfaden.md 8a, decision E-852): the logo at the top and the
// entries below it, on the scrim. The main menu shows its entries in it; the settings and the
// handbook (SidebarScreen) show a back entry and their categories in the same place, so the bar
// stays where it is when they open (drill-down).
Rectangle {
    id: bar

    // The entries below the logo, top to bottom (in a ColumnLayout).
    default property alias entries: column.data

    color: Theme.scrim

    ColumnLayout {
        id: column

        anchors.fill: parent
        anchors.topMargin: Ui.space.xl
        anchors.bottomMargin: Ui.space.l
        spacing: Ui.space.l

        GameLogo {
            Layout.fillWidth: true
            Layout.leftMargin: Ui.space.l
            Layout.rightMargin: Ui.space.l
            font.pixelSize: Ui.type.logoSmall
        }
    }
}
