pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The game settings (decision E-845, docs/UI-Leitfaden.md 8): the categories in a list on the left
// (graphics with the display options of decision E-835, audio, controls, controller, interface,
// texture packs, advanced with debug and diagnostics), the chosen page on the right, scrolling as a
// whole, and "Done" in the footer. Changes apply at once and are stored when the screen closes; key
// and display changes at once. The pad's shoulders and Ctrl+Tab switch the category.
Popup {
    id: screen

    enum Page {
        Graphics,
        Audio,
        Controls,
        Controller,
        Interface,
        TexturePacks,
        Advanced
    }

    required property GameController game
    // The page to open on (SettingsScreen.Graphics, ...; pageOfOverlay() for an overlay).
    property int startPage: SettingsScreen.Graphics
    // Room kept free below the screen for the pad's hints (PadHints, about 40 px high) while they
    // show: the screen moves up by it and gets shorter if it must.
    readonly property real padHintsRoom: game.gamepad.focusShown ? Ui.size.padHints : 0

    // The page an overlay opens: the settings and the display options the graphics page (it begins
    // with them), the debug options the advanced page.
    function pageOfOverlay(overlay: int): int {
        switch (overlay) {
        case GameController.Overlay.Audio:
            return SettingsScreen.Audio;
        case GameController.Overlay.Controls:
            return SettingsScreen.Controls;
        case GameController.Overlay.Gamepad:
            return SettingsScreen.Controller;
        case GameController.Overlay.Interface:
            return SettingsScreen.Interface;
        case GameController.Overlay.TexturePacks:
            return SettingsScreen.TexturePacks;
        case GameController.Overlay.Debug:
            return SettingsScreen.Advanced;
        default:
            return SettingsScreen.Graphics;
        }
    }

    x: (parent.width - width) / 2
    y: (parent.height - height - padHintsRoom) / 2
    width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.l)
    height: parent.height - 2 * Ui.screen.marginVertical - padHintsRoom
    modal: true
    focus: true
    padding: 0
    background: ScreenFrame {}

    onAboutToShow: {
        nav.currentIndex = startPage;
        controlsPage.clear();
    }
    onClosed: {
        controlsPage.cancelCapture();
        game.settings.saveSettings();
    }

    RowLayout {
        id: content

        // The pad's shoulders switch the category (QcMenuNavigator asks the items above the focus).
        function gamepadCommand(command: string): bool {
            return nav.takeCommand(command);
        }

        anchors.fill: parent
        focus: true
        spacing: 0
        // Keys the controls leave end here; Esc still closes the screen, Tab moves the focus
        // (Ctrl+Tab and Ctrl+Shift+Tab switch the category: shortcuts of the NavList).
        Keys.onPressed: event => {
            event.accepted = event.key !== Qt.Key_Escape && event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
        }

        // The category list keeps its width (its rows would make it fill otherwise).
        ColumnLayout {
            readonly property real navWidth: screen.parent && screen.parent.width < Ui.screen.narrow ? Ui.size.navNarrow : Ui.size.nav

            Layout.fillWidth: false
            Layout.preferredWidth: navWidth
            Layout.maximumWidth: navWidth
            Layout.fillHeight: true
            spacing: Ui.space.l

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Ui.space.xl
                Layout.leftMargin: Ui.space.l
                Layout.rightMargin: Ui.space.s
                text: qsTr("Settings")
                font.pixelSize: Ui.type.title
                font.bold: true
                color: Theme.text
                // In the narrow category list the title shrinks rather than being cut.
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: Ui.type.section
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }

            NavList {
                id: nav

                objectName: "settingsNav"
                Layout.fillWidth: true
                model: [qsTr("Graphics"), qsTr("Audio"), qsTr("Controls"), qsTr("Controller"), qsTr("Interface"),
                    qsTr("Texture packs"), qsTr("Advanced")]
            }

            // Keeps the list at the top.
            Item {
                Layout.fillHeight: true
            }
        }

        Rectangle {
            Layout.preferredWidth: Ui.size.line
            Layout.fillHeight: true
            color: Theme.separator
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            StackLayout {
                id: pages

                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: Ui.space.xl
                Layout.leftMargin: Ui.space.xl
                Layout.rightMargin: Ui.space.s
                currentIndex: nav.currentIndex
                onCurrentIndexChanged: controlsPage.cancelCapture()

                GraphicsPage {
                    game: screen.game
                }
                AudioPage {
                    game: screen.game
                }
                ControlsPage {
                    id: controlsPage

                    game: screen.game
                }
                GamepadPage {
                    game: screen.game
                }
                InterfacePage {
                    game: screen.game
                }
                TexturePacksPage {
                    game: screen.game
                }
                AdvancedPage {
                    game: screen.game
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Ui.size.line
                color: Theme.separator
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: Ui.space.l
                Layout.leftMargin: Ui.space.xl
                Layout.rightMargin: Ui.space.xl
                spacing: Ui.space.s

                Item {
                    Layout.fillWidth: true
                }
                GameButton {
                    kind: GameButton.Primary
                    // The pad's focus starts on the categories, not on "Done".
                    highlighted: false
                    large: true
                    text: qsTr("Done")
                    onClicked: screen.close()
                }
            }
        }
    }
}
