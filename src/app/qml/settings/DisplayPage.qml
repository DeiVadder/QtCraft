pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The display options (decision E-835): window mode and screen, VSync and the frame rate limits for
// playing and for menus, a paused game and a window in the background. Every change is stored at
// once; VSync takes effect at the next start.
GridLayout {
    id: page

    required property GameController game
    readonly property DisplaySettings display: game.settings.display
    // The limits on offer, in the order of the boxes.
    readonly property var limitModel: [
        { text: qsTr("Unlimited"), value: DisplaySettings.FrameLimit.Unlimited },
        { text: qsTr("%1 FPS").arg(30), value: DisplaySettings.FrameLimit.Fps30 },
        { text: qsTr("%1 FPS").arg(60), value: DisplaySettings.FrameLimit.Fps60 },
        { text: qsTr("%1 FPS").arg(90), value: DisplaySettings.FrameLimit.Fps90 },
        { text: qsTr("%1 FPS").arg(120), value: DisplaySettings.FrameLimit.Fps120 },
        { text: qsTr("%1 FPS").arg(144), value: DisplaySettings.FrameLimit.Fps144 }
    ]

    // The boxes show the stored values whenever the page opens (the fullscreen key may have
    // switched the mode meanwhile).
    function refresh() {
        modeBox.currentIndex = modeBox.indexOfValue(display.windowMode);
        screenBox.currentIndex = display.screen;
        limitBox.currentIndex = limitBox.indexOfValue(display.frameLimit);
        backgroundLimitBox.currentIndex = backgroundLimitBox.indexOfValue(display.backgroundFrameLimit);
    }

    columns: 2
    columnSpacing: Ui.px(12)
    rowSpacing: Ui.px(10)
    Component.onCompleted: refresh()
    onVisibleChanged: if (visible) refresh()

    Connections {
        target: page.display

        function onChanged() {
            page.refresh();
        }
    }

    Label {
        // With the fullscreen key (on a Mac fn+F11) while it has one.
        text: page.game.settings.keys.labels.fullscreen ? qsTr("Window mode (%1)").arg(page.game.settings.keys.labels.fullscreen)
                                                        : qsTr("Window mode")
    }
    ComboBox {
        id: modeBox

        objectName: "windowModeBox"
        Accessible.name: qsTr("Window mode")
        Layout.fillWidth: true
        textRole: "text"
        valueRole: "value"
        model: [
            { text: qsTr("Window"), value: DisplaySettings.WindowMode.Windowed },
            { text: qsTr("Borderless fullscreen"), value: DisplaySettings.WindowMode.Borderless },
            { text: qsTr("Fullscreen"), value: DisplaySettings.WindowMode.Fullscreen }
        ]
        onActivated: page.display.windowMode = currentValue
    }

    Label {
        text: qsTr("Screen")
    }
    ComboBox {
        id: screenBox

        objectName: "screenBox"
        Accessible.name: qsTr("Screen")
        Layout.fillWidth: true
        model: page.display.screens
        enabled: count > 1
        onActivated: index => page.display.screen = index
    }

    CheckBox {
        objectName: "vsyncBox"
        Layout.columnSpan: 2
        text: qsTr("VSync (wait for the display; no tearing)")
        checked: page.display.vsync
        onToggled: page.display.vsync = checked
    }
    Label {
        objectName: "vsyncRestart"
        Layout.columnSpan: 2
        Layout.fillWidth: true
        visible: page.display.restartNeeded
        text: qsTr("VSync takes effect after a restart of the game.")
        wrapMode: Text.Wrap
        color: Theme.warning
    }

    Label {
        text: qsTr("Frame rate limit")
    }
    ComboBox {
        id: limitBox

        objectName: "frameLimitBox"
        Accessible.name: qsTr("Frame rate limit")
        Layout.fillWidth: true
        textRole: "text"
        valueRole: "value"
        model: page.limitModel
        onActivated: page.display.frameLimit = currentValue
    }

    Label {
        text: qsTr("In menus and in the background")
    }
    ComboBox {
        id: backgroundLimitBox

        objectName: "backgroundFrameLimitBox"
        Accessible.name: qsTr("Frame rate limit in menus and in the background")
        Layout.fillWidth: true
        textRole: "text"
        valueRole: "value"
        model: page.limitModel
        onActivated: page.display.backgroundFrameLimit = currentValue
    }

    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        text: qsTr("A lower limit keeps the graphics card cooler and quieter; VSync without a limit gives the smoothest picture. The second limit applies to the world behind the pause menu, to the character previews and while another window has the focus.")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    Label {
        Layout.columnSpan: 2
        Layout.fillWidth: true
        visible: !page.display.controlsWindow
        text: qsTr("This run places its window itself (a test or measurement); the choices apply at the next normal start.")
        wrapMode: Text.Wrap
        opacity: 0.7
    }

    // Keeps the rows at the top.
    Item {
        Layout.columnSpan: 2
        Layout.fillHeight: true
    }
}
