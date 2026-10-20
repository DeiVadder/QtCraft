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
    readonly property real padHintsRoom: game.gamepad.focusShown ? Ui.px(48) : 0

    x: (parent.width - width) / 2
    y: (parent.height - height - padHintsRoom) / 2
    width: Math.min(parent.width - Ui.px(64), Ui.px(720))
    height: Math.min(implicitHeight, parent.height - Ui.px(32) - padHintsRoom)
    modal: true
    focus: true
    padding: Ui.px(20)

    onAboutToShow: {
        languageBox.currentIndex = languageBox.indexOfValue(game.settings.language);
        cameraBox.currentIndex = cameraBox.indexOfValue(game.settings.cameraMode);
        uiScaleBox.currentIndex = uiScaleBox.indexOfValue(game.settings.uiScale);
        textureFilteringBox.currentIndex = textureFilteringBox.indexOfValue(game.settings.textureFiltering);
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
        spacing: Ui.px(10)

        Label {
            text: qsTr("Settings")
            font.pixelSize: Ui.px(24)
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
                    width: generalScroll.width - Ui.px(14)
                    columns: 3
                    columnSpacing: Ui.px(12)
                    rowSpacing: Ui.px(10)

                    Label {
                        text: qsTr("View distance")
                    }
                    Slider {
                        id: viewDistance

                        Accessible.name: qsTr("View distance")
                        Layout.fillWidth: true
                        from: 2
                        to: 16
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: screen.game.settings.viewDistance
                        onMoved: screen.game.settings.viewDistance = value
                    }
                    Label {
                        Layout.preferredWidth: Ui.px(90)
                        text: qsTr("%n chunk(s)", "", viewDistance.value)
                    }

                    Label {
                        text: qsTr("Field of view")
                    }
                    Slider {
                        id: fieldOfView

                        Accessible.name: qsTr("Field of view")
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
                        Accessible.name: qsTr("FOV effects")
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

                    // Filtering of surfaces seen at a flat angle (decision E-721): more samples keep
                    // distant ground sharper and cost more GPU time.
                    Label {
                        text: qsTr("Texture filtering")
                    }
                    ComboBox {
                        id: textureFilteringBox

                        objectName: "textureFilteringBox"
                        Accessible.name: qsTr("Texture filtering")
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: qsTr("Off"), value: GameSettings.TextureFiltering.Off },
                            { text: qsTr("%1×").arg(2), value: GameSettings.TextureFiltering.X2 },
                            { text: qsTr("%1×").arg(4), value: GameSettings.TextureFiltering.X4 },
                            { text: qsTr("%1×").arg(8), value: GameSettings.TextureFiltering.X8 }
                        ]
                        onActivated: screen.game.settings.textureFiltering = currentValue
                    }

                    Label {
                        text: qsTr("Mouse sensitivity")
                    }
                    Slider {
                        id: sensitivity

                        Accessible.name: qsTr("Mouse sensitivity")
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

                    CheckBox {
                        Layout.columnSpan: 3
                        text: qsTr("Hiding the HUD (%1) hides the hand too").arg(screen.game.settings.keys.labels.hideHud)
                        checked: screen.game.settings.photoHidesHand
                        onToggled: screen.game.settings.photoHidesHand = checked
                    }

                    Label {
                        text: qsTr("Master volume")
                    }
                    Slider {
                        id: masterVolume

                        Accessible.name: qsTr("Master volume")
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

                        Accessible.name: qsTr("Effects")
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

                        Accessible.name: qsTr("Music")
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

                        Accessible.name: qsTr("Ambience")
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

                        Accessible.name: qsTr("View")
                        Layout.fillWidth: true
                        Layout.columnSpan: 2
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: qsTr("First person"), value: GameSettings.CameraMode.FirstPerson },
                            { text: qsTr("Third person, behind"), value: GameSettings.CameraMode.Behind },
                            { text: qsTr("Third person, front"), value: GameSettings.CameraMode.Front }
                        ]
                        onActivated: screen.game.settings.cameraMode = currentValue
                    }

                    Label {
                        text: qsTr("Language")
                    }
                    ComboBox {
                        id: languageBox

                        Accessible.name: qsTr("Language")
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

                    // The size of the HUD and the screens (decision E-725); Auto follows the window.
                    Label {
                        text: qsTr("Interface size")
                    }
                    ComboBox {
                        id: uiScaleBox

                        objectName: "uiScaleBox"
                        Accessible.name: qsTr("Interface size")
                        Layout.fillWidth: true
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { text: qsTr("Auto"), value: UiScaling.Step.Auto },
                            { text: qsTr("%1 %").arg(75), value: UiScaling.Step.Percent75 },
                            { text: qsTr("%1 %").arg(100), value: UiScaling.Step.Percent100 },
                            { text: qsTr("%1 %").arg(125), value: UiScaling.Step.Percent125 },
                            { text: qsTr("%1 %").arg(150), value: UiScaling.Step.Percent150 },
                            { text: qsTr("%1 %").arg(175), value: UiScaling.Step.Percent175 },
                            { text: qsTr("%1 %").arg(200), value: UiScaling.Step.Percent200 }
                        ]
                        onActivated: screen.game.settings.uiScale = currentValue
                    }
                    // The size in use (Auto's choice, or less where the window is too small).
                    Label {
                        text: qsTr("%1 %").arg(Math.round(UiScaling.scale * 100))
                    }

                    // A window too small for the chosen size shows the screens as large as fits.
                    Label {
                        objectName: "uiScaleLimited"
                        Layout.columnSpan: 3
                        Layout.fillWidth: true
                        visible: UiScaling.scale < UiScaling.requested
                        text: qsTr("Screens show at %1 % in this window; the HUD at %2 %. A larger window shows them larger.")
                              .arg(Math.round(UiScaling.scale * 100)).arg(Math.round(UiScaling.hudScale * 100))
                        wrapMode: Text.Wrap
                        opacity: 0.7
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
