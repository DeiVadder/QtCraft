pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Game settings; stored when the screen closes.
Popup {
    id: screen

    required property GameController game

    anchors.centerIn: parent
    width: Math.min(parent.width - 64, 560)
    modal: true
    padding: 20

    onAboutToShow: {
        languageBox.currentIndex = languageBox.indexOfValue(game.language);
        cameraBox.currentIndex = cameraBox.indexOfValue(game.cameraMode);
    }
    onClosed: game.saveSettings()

    GridLayout {
        anchors.fill: parent
        columns: 3
        columnSpacing: 12
        rowSpacing: 10

        Label {
            Layout.columnSpan: 3
            text: qsTr("Settings")
            font.pixelSize: 24
            font.bold: true
        }

        Label {
            text: qsTr("View distance")
        }
        Slider {
            id: viewDistance

            Layout.fillWidth: true
            from: 2
            to: 16
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: screen.game.viewDistance
            onMoved: screen.game.viewDistance = value
        }
        Label {
            Layout.preferredWidth: 90
            text: qsTr("%n chunk(s)", "", viewDistance.value)
        }

        Label {
            text: qsTr("Field of view")
        }
        Slider {
            id: fieldOfView

            Layout.fillWidth: true
            from: 50
            to: 110
            stepSize: 1
            value: screen.game.fieldOfView
            onMoved: screen.game.fieldOfView = value
        }
        Label {
            text: qsTr("%1°").arg(Math.round(fieldOfView.value))
        }

        Label {
            text: qsTr("Mouse sensitivity")
        }
        Slider {
            id: sensitivity

            Layout.fillWidth: true
            from: 0.02
            to: 1.0
            value: screen.game.mouseSensitivity
            onMoved: screen.game.mouseSensitivity = value
        }
        Label {
            text: sensitivity.value.toLocaleString(Qt.locale(), "f", 2)
        }

        CheckBox {
            Layout.columnSpan: 3
            text: qsTr("Invert mouse")
            checked: screen.game.invertMouse
            onToggled: screen.game.invertMouse = checked
        }

        CheckBox {
            Layout.columnSpan: 3
            text: qsTr("Show frame rate and position")
            checked: screen.game.showDebug
            onToggled: screen.game.showDebug = checked
        }

        Label {
            text: qsTr("View")
        }
        ComboBox {
            id: cameraBox

            Layout.fillWidth: true
            Layout.columnSpan: 2
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("First person"), value: "first" },
                { text: qsTr("Third person, behind"), value: "behind" },
                { text: qsTr("Third person, front"), value: "front" }
            ]
            onActivated: screen.game.cameraMode = currentValue
        }

        Label {
            text: qsTr("Language")
        }
        ComboBox {
            id: languageBox

            Layout.fillWidth: true
            Layout.columnSpan: 2
            textRole: "text"
            valueRole: "value"
            // Language names are shown in their own language.
            model: [
                { text: qsTr("System language"), value: "" },
                { text: "English", value: "en" },
                { text: "Deutsch", value: "de" }
            ]
            onActivated: screen.game.language = currentValue
        }

        Label {
            Layout.columnSpan: 3
            Layout.fillWidth: true
            text: qsTr("The view distance applies when a world is started.")
            wrapMode: Text.Wrap
            opacity: 0.7
        }

        Button {
            Layout.columnSpan: 3
            Layout.alignment: Qt.AlignRight
            text: qsTr("Done")
            highlighted: true
            onClicked: screen.close()
        }
    }
}
