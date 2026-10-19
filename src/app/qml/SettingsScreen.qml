pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// Game settings on five pages: general, controls (the key table), controller (the gamepad, decision
// E-610), debug (the debug display and its visualisations) and texture packs; stored when the
// screen closes, key changes at once.
Popup {
    id: screen

    required property GameController game
    // The page to open on: 0 general, 1 controls (overlay "controls"), 2 controller (overlay
    // "gamepad"), 3 debug (overlay "debug"), 4 texture packs (overlay "texturepacks").
    property int startPage
    // Room kept free below the screen for the pad's hints (PadHints, about 40 px high) while they
    // show: the screen moves up by it and gets shorter if it must.
    readonly property real padHintsRoom: game.gamepad.focusShown ? 48 : 0

    x: (parent.width - width) / 2
    y: (parent.height - height - padHintsRoom) / 2
    width: Math.min(parent.width - 64, 720)
    height: Math.min(implicitHeight, parent.height - 32 - padHintsRoom)
    modal: true
    focus: true
    padding: 20

    onAboutToShow: {
        languageBox.currentIndex = languageBox.indexOfValue(game.settings.language);
        cameraBox.currentIndex = cameraBox.indexOfValue(game.settings.cameraMode);
        pages.currentIndex = startPage;
        controlsPage.clear();
    }
    onClosed: {
        controlsPage.cancelCapture();
        game.settings.saveSettings();
    }

    ColumnLayout {
        anchors.fill: parent
        focus: true
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus.
        Keys.onPressed: event => event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab
                                                  && event.key !== Qt.Key_Backtab
        spacing: 10

        Label {
            text: qsTr("Settings")
            font.pixelSize: 24
            font.bold: true
        }

        TabBar {
            id: tabs

            Layout.fillWidth: true
            currentIndex: pages.currentIndex
            onCurrentIndexChanged: pages.currentIndex = currentIndex

            TabButton {
                text: qsTr("General")
            }
            TabButton {
                text: qsTr("Controls")
            }
            TabButton {
                objectName: "gamepadTab"
                text: qsTr("Controller")
            }
            TabButton {
                text: qsTr("Debug")
            }
            TabButton {
                objectName: "texturePacksTab"
                text: qsTr("Texture packs")
            }
        }

        StackLayout {
            id: pages

            Layout.fillWidth: true
            Layout.fillHeight: true
            onCurrentIndexChanged: controlsPage.cancelCapture()

            // The general page scrolls in a window too low for all its rows.
            Flickable {
                id: generalScroll

                implicitHeight: generalPage.implicitHeight
                clip: true
                contentWidth: width
                contentHeight: generalPage.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ThemedScrollBar {}

                GridLayout {
                    id: generalPage

                    // A gutter on the right for the scroll bar.
                    width: generalScroll.width - 14
                    columns: 3
                    columnSpacing: 12
                    rowSpacing: 10

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
                        value: screen.game.settings.viewDistance
                        onMoved: screen.game.settings.viewDistance = value
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
                        value: screen.game.settings.fieldOfView
                        onMoved: screen.game.settings.fieldOfView = value
                    }
                    Label {
                        text: qsTr("%1°").arg(Math.round(fieldOfView.value))
                    }

                    // How much the view widens while sprinting (0 % keeps it still).
                    Label {
                        text: qsTr("FOV effects")
                    }
                    Slider {
                        id: fovEffects

                        objectName: "fovEffects"
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        stepSize: 0.05
                        value: screen.game.settings.fovEffects
                        onMoved: screen.game.settings.fovEffects = value
                    }
                    Label {
                        text: qsTr("%1 %").arg(Math.round(fovEffects.value * 100))
                    }

                    Label {
                        text: qsTr("Mouse sensitivity")
                    }
                    Slider {
                        id: sensitivity

                        Layout.fillWidth: true
                        from: 0.02
                        to: 1.0
                        value: screen.game.settings.mouseSensitivity
                        onMoved: screen.game.settings.mouseSensitivity = value
                    }
                    Label {
                        text: sensitivity.value.toLocaleString(Qt.locale(), "f", 2)
                    }

                    CheckBox {
                        Layout.columnSpan: 3
                        text: qsTr("Invert mouse")
                        checked: screen.game.settings.invertMouse
                        onToggled: screen.game.settings.invertMouse = checked
                    }

                    CheckBox {
                        Layout.columnSpan: 3
                        text: qsTr("Show labels in the world (%1)").arg(screen.game.settings.keys.labels.labels)
                        checked: screen.game.settings.showLabels
                        onToggled: screen.game.settings.showLabels = checked
                    }

                    Label {
                        text: qsTr("Master volume")
                    }
                    Slider {
                        id: masterVolume

                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: screen.game.settings.audio.masterVolume
                        onMoved: screen.game.settings.audio.masterVolume = value
                    }
                    Label {
                        text: qsTr("%1 %").arg(Math.round(masterVolume.value * 100))
                    }

                    Label {
                        text: qsTr("Effects")
                    }
                    Slider {
                        id: effectsVolume

                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: screen.game.settings.audio.effectsVolume
                        onMoved: screen.game.settings.audio.effectsVolume = value
                    }
                    Label {
                        text: qsTr("%1 %").arg(Math.round(effectsVolume.value * 100))
                    }

                    Label {
                        text: qsTr("Music")
                    }
                    Slider {
                        id: musicVolume

                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: screen.game.settings.audio.musicVolume
                        onMoved: screen.game.settings.audio.musicVolume = value
                    }
                    Label {
                        text: qsTr("%1 %").arg(Math.round(musicVolume.value * 100))
                    }

                    Label {
                        text: qsTr("Ambience")
                    }
                    Slider {
                        id: ambientVolume

                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: screen.game.settings.audio.ambientVolume
                        onMoved: screen.game.settings.audio.ambientVolume = value
                    }
                    Label {
                        text: qsTr("%1 %").arg(Math.round(ambientVolume.value * 100))
                    }

                    CheckBox {
                        Layout.columnSpan: 3
                        text: qsTr("Mute all sounds")
                        checked: screen.game.settings.audio.muted
                        onToggled: screen.game.settings.audio.muted = checked
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
                        onActivated: screen.game.settings.cameraMode = currentValue
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
                        onActivated: screen.game.settings.language = currentValue
                    }

                    Label {
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        text: qsTr("The view distance applies when a world is started.")
                        wrapMode: Text.Wrap
                        opacity: 0.7
                    }
                }
            }

            ControlsPage {
                id: controlsPage

                keys: screen.game.settings.keys
            }

            GamepadPage {
                game: screen.game
            }

            DebugOptionsPage {
                game: screen.game
            }

            TexturePacksPage {
                game: screen.game
            }
        }

        Button {
            Layout.alignment: Qt.AlignRight
            text: qsTr("Done")
            highlighted: true
            onClicked: screen.close()
        }
    }
}
