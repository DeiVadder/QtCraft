import QtQuick
import QtCraft

// The interface page of the settings (decision E-845): language and interface size first (what a
// player looks for in the first minutes), then the view and what the screen shows of the world.
SettingsPage {
    id: page

    required property GameController game

    // The lists show the stored values whenever the screen opens.
    function refresh() {
        languageBox.currentIndex = languageBox.indexOfValue(game.settings.language);
        // Auto (older settings files) is 100 %.
        const scale = game.settings.uiScale === UiScaling.Step.Auto ? UiScaling.Step.Percent100 : game.settings.uiScale;
        uiScaleBox.currentIndex = uiScaleBox.indexOfValue(scale);
        cameraBox.currentIndex = cameraBox.indexOfValue(game.settings.cameraMode);
    }

    title: qsTr("Interface")
    Component.onCompleted: refresh()
    onVisibleChanged: if (visible) refresh()

    SettingGroup {
        title: qsTr("Language and size")

        SettingDropdown {
            id: languageBox

            label: qsTr("Language")
            textRole: "text"
            valueRole: "value"
            // Language names are shown in their own language.
            model: [
                { text: qsTr("System language"), value: "" },
                { text: "English", value: "en" },
                { text: "Deutsch", value: "de" }
            ]
            onActivated: page.game.settings.language = currentValue
        }

        // The size of the HUD and the screens (decisions E-725, E-875), relative to the window: a
        // large window shows every step larger. A window too small for the chosen size shows the
        // screens as large as fits.
        SettingDropdown {
            id: uiScaleBox

            objectName: "uiScaleBox"
            label: qsTr("Interface size")
            help: UiScaling.scale < UiScaling.requested
                  ? qsTr("Screens show at %1 % in this window; the HUD at %2 %. A larger window shows them larger.")
                        .arg(Math.round(UiScaling.scale * 100)).arg(Math.round(UiScaling.hudScale * 100))
                  : currentIndex >= 0 && Math.round(UiScaling.scale * 100) !== model[currentIndex].percent
                    ? qsTr("Now %1 % (grows with large windows)").arg(Math.round(UiScaling.scale * 100)) : ""
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("%1 %").arg(75), value: UiScaling.Step.Percent75, percent: 75 },
                { text: qsTr("%1 %").arg(100), value: UiScaling.Step.Percent100, percent: 100 },
                { text: qsTr("%1 %").arg(125), value: UiScaling.Step.Percent125, percent: 125 },
                { text: qsTr("%1 %").arg(150), value: UiScaling.Step.Percent150, percent: 150 },
                { text: qsTr("%1 %").arg(175), value: UiScaling.Step.Percent175, percent: 175 },
                { text: qsTr("%1 %").arg(200), value: UiScaling.Step.Percent200, percent: 200 }
            ]
            onActivated: page.game.settings.uiScale = currentValue
        }
    }

    SettingGroup {
        title: qsTr("View")

        SettingDropdown {
            id: cameraBox

            label: qsTr("Camera")
            textRole: "text"
            valueRole: "value"
            model: [
                { text: qsTr("First person"), value: GameSettings.CameraMode.FirstPerson },
                { text: qsTr("Third person, behind"), value: GameSettings.CameraMode.Behind },
                { text: qsTr("Third person, front"), value: GameSettings.CameraMode.Front }
            ]
            onActivated: page.game.settings.cameraMode = currentValue
        }

        SettingSwitch {
            label: qsTr("Labels in the world")
            help: qsTr("Names floating over places, such as the areas of the showcase")
            key: page.game.settings.keys.labels.labels
            checked: page.game.settings.showLabels
            onToggled: page.game.settings.showLabels = checked
        }

        SettingSwitch {
            label: qsTr("Hide the hand with the HUD")
            help: qsTr("When the HUD is hidden for a picture")
            key: page.game.settings.keys.labels.hideHud
            checked: page.game.settings.photoHidesHand
            onToggled: page.game.settings.photoHidesHand = checked
        }
    }

    SettingGroup {
        title: qsTr("Pause")

        SettingSwitch {
            label: qsTr("Pause when the window loses focus")
            help: qsTr("Opens the pause menu of a single-player world")
            checked: page.game.settings.pauseOnFocusLoss
            onToggled: page.game.settings.pauseOnFocusLoss = checked
        }
    }
}
