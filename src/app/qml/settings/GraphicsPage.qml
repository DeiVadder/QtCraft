pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The graphics page of the settings (decision E-845): the display options first (window mode and
// screen, VSync and the frame rate limits for playing and for menus, a paused game and a window in
// the background; decision E-835), then the world (view distance, field of view and its widening
// while sprinting) and the quality (texture filtering, decision E-721; the render mode with the
// quality and exposure of the realistic mode, decision E-899). Every change is stored at once; VSync
// takes effect at the next start, the view distance at the next world start.
SettingsPage {
    id: page

    required property GameController game
    readonly property DisplaySettings display: game.settings.display
    readonly property RenderSettings rendering: game.settings.rendering
    // The limits on offer, in the order of the lists.
    readonly property var limitModel: [
        { text: qsTr("Unlimited"), value: DisplaySettings.FrameLimit.Unlimited },
        { text: qsTr("%1 FPS").arg(30), value: DisplaySettings.FrameLimit.Fps30 },
        { text: qsTr("%1 FPS").arg(60), value: DisplaySettings.FrameLimit.Fps60 },
        { text: qsTr("%1 FPS").arg(90), value: DisplaySettings.FrameLimit.Fps90 },
        { text: qsTr("%1 FPS").arg(120), value: DisplaySettings.FrameLimit.Fps120 },
        { text: qsTr("%1 FPS").arg(144), value: DisplaySettings.FrameLimit.Fps144 }
    ]

    // The lists show the stored values whenever the page opens (the fullscreen key may have
    // switched the mode meanwhile).
    function refresh() {
        screenBox.currentIndex = display.screen;
        limitBox.currentIndex = limitBox.indexOfValue(display.frameLimit);
        backgroundLimitBox.currentIndex = backgroundLimitBox.indexOfValue(display.backgroundFrameLimit);
    }

    title: qsTr("Graphics")
    Component.onCompleted: refresh()
    onVisibleChanged: if (visible) refresh()

    Connections {
        target: page.display

        function onChanged() {
            page.refresh();
        }
    }

    SettingGroup {
        title: qsTr("Display")

        SettingChoice {
            objectName: "windowModeChoice"
            label: qsTr("Window mode")
            // The fullscreen key (on a Mac fn+F11) while it has one.
            key: page.game.settings.keys.labels.fullscreen
            model: [
                { text: qsTr("Window"), value: DisplaySettings.WindowMode.Windowed },
                { text: qsTr("Borderless"), value: DisplaySettings.WindowMode.Borderless },
                { text: qsTr("Fullscreen"), value: DisplaySettings.WindowMode.Fullscreen }
            ]
            currentValue: page.display.windowMode
            onActivated: value => page.display.windowMode = value
        }

        SettingDropdown {
            id: screenBox

            objectName: "screenBox"
            label: qsTr("Screen")
            help: count > 1 ? "" : qsTr("Only one screen is connected")
            model: page.display.screens
            enabled: count > 1
            onActivated: index => page.display.screen = index
        }

        SettingSwitch {
            objectName: "vsyncSwitch"
            label: qsTr("VSync")
            help: qsTr("Waits for the display: no tearing")
            badge: page.display.restartNeeded ? qsTr("after a restart") : ""
            checked: page.display.vsync
            onToggled: page.display.vsync = checked
        }

        SettingDropdown {
            id: limitBox

            objectName: "frameLimitBox"
            label: qsTr("Frame rate limit")
            help: qsTr("A lower limit keeps the graphics card cooler and quieter")
            textRole: "text"
            valueRole: "value"
            model: page.limitModel
            onActivated: page.display.frameLimit = currentValue
        }

        SettingDropdown {
            id: backgroundLimitBox

            objectName: "backgroundFrameLimitBox"
            label: qsTr("In menus and in the background")
            help: qsTr("Behind the pause menu, in character previews and while another window has the focus")
            textRole: "text"
            valueRole: "value"
            model: page.limitModel
            onActivated: page.display.backgroundFrameLimit = currentValue
        }

        Hint {
            Layout.fillWidth: true
            Layout.bottomMargin: Ui.space.xs
            visible: !page.display.controlsWindow
            text: qsTr("This run places its window itself (a test or measurement); the choices apply at the next normal start.")
        }
    }

    SettingGroup {
        title: qsTr("World")

        SettingSlider {
            label: qsTr("View distance")
            badge: qsTr("from the next world start")
            from: 2
            to: 16
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: page.game.settings.viewDistance
            valueText: qsTr("%n chunk(s)", "", Math.round(value))
            onMoved: page.game.settings.viewDistance = value
        }

        SettingSlider {
            label: qsTr("Field of view")
            from: 50
            to: 110
            stepSize: 1
            value: page.game.settings.fieldOfView
            valueText: qsTr("%1°").arg(Math.round(value))
            onMoved: page.game.settings.fieldOfView = value
        }

        // How much the view widens while sprinting (0 % keeps it still).
        SettingSlider {
            objectName: "fovEffects"
            label: qsTr("Field of view when sprinting")
            help: qsTr("How much the view widens; 0 % keeps it still")
            from: 0
            to: 1
            stepSize: 0.05
            value: page.game.settings.fovEffects
            valueText: qsTr("%1 %").arg(Math.round(value * 100))
            onMoved: page.game.settings.fovEffects = value
        }

        // Explosions near the player shake the view (decision E-879).
        SettingSwitch {
            objectName: "cameraShake"
            label: qsTr("Camera shake")
            help: qsTr("Explosions nearby shake the view")
            checked: page.game.settings.cameraShake
            onToggled: page.game.settings.cameraShake = checked
        }
    }

    SettingGroup {
        title: qsTr("Quality")

        // Filtering of surfaces seen at a flat angle (decision E-721): more samples keep distant
        // ground sharper and cost more GPU time.
        SettingChoice {
            objectName: "textureFilteringChoice"
            label: qsTr("Texture filtering")
            help: qsTr("Keeps distant ground sharp; more costs more GPU time")
            model: [
                { text: qsTr("Off"), value: GameSettings.TextureFiltering.Off },
                { text: qsTr("%1×").arg(2), value: GameSettings.TextureFiltering.X2 },
                { text: qsTr("%1×").arg(4), value: GameSettings.TextureFiltering.X4 },
                { text: qsTr("%1×").arg(8), value: GameSettings.TextureFiltering.X8 }
            ]
            currentValue: page.game.settings.textureFiltering
            onActivated: value => page.game.settings.textureFiltering = value
        }

        // Classic or realistic (decision E-899): the realistic mode draws the world in HDR with
        // height fog, bloom, tonemapping, ambient occlusion and moving lights (decision E-902) and
        // costs more GPU time.
        SettingChoice {
            objectName: "renderModeChoice"
            label: qsTr("Render mode")
            help: qsTr("Realistic adds valley fog, glow, soft shade and light from held torches; it costs more GPU time")
            model: [
                { text: qsTr("Classic"), value: RenderSettings.Mode.Classic },
                { text: qsTr("Realistic"), value: RenderSettings.Mode.Realistic }
            ]
            currentValue: page.rendering.mode
            onActivated: value => page.rendering.mode = value
        }

        SettingChoice {
            objectName: "renderQualityChoice"
            label: qsTr("Realistic quality")
            help: qsTr("Low without glow and soft shade, High with the widest glow and the most moving lights")
            enabled: page.rendering.mode === RenderSettings.Mode.Realistic
            model: [
                { text: qsTr("Low"), value: RenderSettings.Quality.Low },
                { text: qsTr("Medium"), value: RenderSettings.Quality.Medium },
                { text: qsTr("High"), value: RenderSettings.Quality.High }
            ]
            currentValue: page.rendering.quality
            onActivated: value => page.rendering.quality = value
        }

        SettingSlider {
            objectName: "exposure"
            label: qsTr("Exposure")
            help: qsTr("Brightness of the realistic mode")
            enabled: page.rendering.mode === RenderSettings.Mode.Realistic
            from: page.rendering.minExposure
            to: page.rendering.maxExposure
            stepSize: 0.05
            value: page.rendering.exposure
            valueText: qsTr("%1×").arg(value.toFixed(2))
            onMoved: page.rendering.exposure = value
        }
    }
}
