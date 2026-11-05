// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The question before a world made with an older world generator opens (decisions E-1013, E-1014):
// which dimensions are older, what a switch keeps, that a backup comes first; "Keep version" (the
// default, where the focus starts, with "Don't ask again"), "Switch" with hard borders and, greyed
// with a tooltip, "Switch with transition" (a later version). While switching, the stage with its
// bar and "Cancel" (focused; not while writing); after a failure, the reason and "Close".
// Shown while GeneratorUpgrade.state is not Idle.
ModalDialog {
    id: dialog

    required property GeneratorUpgrade upgrade
    readonly property bool asking: upgrade.state === GeneratorUpgrade.Asking
    readonly property bool converting: upgrade.state === GeneratorUpgrade.Converting
    readonly property bool failed: upgrade.state === GeneratorUpgrade.Failed

    // The button that takes the focus in the dialog's present state (a tab focus shows the outline).
    // A footer button at the right edge of the dialog.
    component RightEdgeButton: GameButton {
        Layout.bottomMargin: Ui.space.xl
        Layout.rightMargin: Ui.space.xl
    }

    function focusButton() {
        const button = dialog.converting ? cancelButton : dialog.failed ? closeButton : keepButton;
        button.forceActiveFocus(Qt.TabFocusReason);
    }

    visible: upgrade.state !== GeneratorUpgrade.Idle
    closePolicy: Popup.NoAutoClose
    onOpened: {
        dontAsk.checked = false;
        focusButton();
    }
    onAskingChanged: focusButton()
    onConvertingChanged: focusButton()
    onFailedChanged: focusButton()

    header: Label {
        leftPadding: Ui.space.xl
        rightPadding: Ui.space.xl
        topPadding: Ui.space.xl
        text: dialog.converting ? qsTr("Switching the world generator") : qsTr("Newer world generator")
        font.pixelSize: Ui.type.title
        font.bold: true
        color: Theme.text
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    ColumnLayout {
        width: dialog.availableWidth
        spacing: Ui.space.m

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            text: qsTr("“%1” comes from an older world generator (%2).").arg(dialog.upgrade.worldName).arg(dialog.upgrade.changesText)
            font.pixelSize: Ui.type.body
            color: Theme.text
        }
        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: qsTr("Everything you have explored stays as it is. Only new land comes from the new generator. An edge may appear at the border.")
            font.pixelSize: Ui.type.body
            color: Theme.text
        }
        Hint {
            Layout.fillWidth: true
            visible: dialog.asking
            text: qsTr("A backup is made first.")
        }
        SettingCheck {
            id: dontAsk

            objectName: "generatorDontAsk"
            visible: dialog.asking
            text: qsTr("Don't ask again")
        }
        Label {
            Layout.fillWidth: true
            visible: dialog.converting
            wrapMode: Text.Wrap
            text: dialog.upgrade.stageText
            font.pixelSize: Ui.type.body
            color: Theme.text
        }
        LoadingBar {
            Layout.fillWidth: true
            visible: dialog.converting
            fraction: Math.max(0, dialog.upgrade.progress)
            waiting: dialog.upgrade.progress < 0
        }
        Hint {
            Layout.fillWidth: true
            visible: dialog.failed
            kind: Hint.Error
            text: dialog.upgrade.error
        }
    }

    footer: RowLayout {
        spacing: Ui.space.s

        Item {
            Layout.fillWidth: true
        }
        // A disabled button takes no hover: the tooltip hangs on the item around it.
        Item {
            Layout.bottomMargin: Ui.space.xl
            visible: dialog.asking
            implicitWidth: blendButton.implicitWidth
            implicitHeight: blendButton.implicitHeight

            GameButton {
                id: blendButton

                objectName: "generatorBlendButton"
                anchors.fill: parent
                enabled: dialog.upgrade.blendAvailable
                text: qsTr("Switch with transition")
            }
            HoverHandler {
                id: blendHover
            }
            ThemedToolTip {
                shown: blendHover.hovered
                text: qsTr("Comes with a later version")
            }
        }
        GameButton {
            objectName: "generatorSwitchButton"
            Layout.bottomMargin: Ui.space.xl
            visible: dialog.asking
            text: qsTr("Switch")
            onClicked: dialog.upgrade.switchHard()
        }
        RightEdgeButton {
            id: keepButton

            objectName: "generatorKeepButton"
            visible: dialog.asking
            kind: GameButton.Primary
            text: dialog.upgrade.changes.length === 1 ? qsTr("Keep version %1").arg(dialog.upgrade.changes[0].from) : qsTr("Keep versions")
            onClicked: dialog.upgrade.keep(dontAsk.checked)
        }
        RightEdgeButton {
            id: cancelButton

            objectName: "generatorCancelButton"
            visible: dialog.converting
            // Writing runs to its end: a few files, half of them would be worse.
            enabled: !dialog.upgrade.cancelling && dialog.upgrade.stage !== GeneratorUpgrade.Write
            text: qsTr("Cancel")
            onClicked: dialog.upgrade.cancel()
        }
        RightEdgeButton {
            id: closeButton

            objectName: "generatorCloseButton"
            visible: dialog.failed
            text: qsTr("Close")
            onClicked: dialog.upgrade.dismiss()
        }
    }
}
