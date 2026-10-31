// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The loading screen of a world start (decision E-943): shown in the frame after the click, over
// the menu's background, until the spawn area is meshed. The title says what is started, the bar the
// share of the whole start, the steps below which one runs (with the chunks of the spawn area as
// counts); everything comes from GameController.loading, nothing is timed here. Needs no input; a
// network game can be cancelled ("Cancel", Escape, the pad's B).
LoadingBackdrop {
    id: screen

    required property LoadingProgress progress
    property bool canCancel: false
    readonly property bool network: progress.kind === LoadingProgress.Network
    readonly property bool newWorld: progress.kind === LoadingProgress.NewWorld

    signal cancelRequested

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Escape && screen.canCancel)
            screen.cancelRequested();
        event.accepted = event.key !== Qt.Key_Tab && event.key !== Qt.Key_Backtab;
    }

    // The counts of a chunk step ("12 / 49") while it runs or once it is done.
    function countText(done: int, needed: int): string {
        return needed > 0 ? qsTr("%1 / %2").arg(done).arg(needed) : "";
    }

    // The text of the step for `stage` (LoadingProgress.Stage), by what is started.
    function stepText(stage: int): string {
        switch (stage) {
        case LoadingProgress.Content:
            return qsTr("Loading the game content");
        case LoadingProgress.World:
            return screen.newWorld ? qsTr("Setting up the world") : qsTr("Opening the world");
        case LoadingProgress.Joining:
            return screen.network ? qsTr("Waiting for the server") : qsTr("Placing the player");
        case LoadingProgress.Chunks:
            return screen.newWorld ? qsTr("Generating the terrain") : qsTr("Loading the terrain");
        }
        return qsTr("Building the view");
    }

    // The counts of the step for `stage`: the chunks arrived or meshed, nothing for the others.
    function stepCount(stage: int): string {
        if (stage === LoadingProgress.Chunks)
            return countText(screen.progress.chunksArrived, screen.progress.chunksNeeded);
        if (stage === LoadingProgress.Meshes)
            return countText(screen.progress.chunksMeshed, screen.progress.chunksNeeded);
        return "";
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Ui.screen.margin, Ui.screen.s)
        spacing: Ui.space.xl

        GameLogo {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: parent.width
            font.pixelSize: Ui.type.logoSmall
        }

        ScreenFrame {
            Layout.fillWidth: true
            implicitHeight: content.implicitHeight + 2 * Ui.space.xl

            ColumnLayout {
                id: content

                anchors.fill: parent
                anchors.margins: Ui.space.xl
                spacing: Ui.space.l

                Label {
                    objectName: "loadingTitle"
                    Layout.fillWidth: true
                    text: screen.network ? qsTr("Joining the server") : screen.newWorld ? qsTr("Creating the world") : qsTr("Loading the world")
                    color: Theme.heading
                    font.pixelSize: Ui.type.title
                    elide: Text.ElideRight
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                LoadingBar {
                    Layout.fillWidth: true
                    fraction: screen.progress.fraction
                    // The steps that hold the GUI thread show the running piece (frozen while they
                    // block, moving again in between).
                    waiting: screen.progress.stage < LoadingProgress.Chunks
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Ui.space.s

                    // One row per stage from Content to Meshes (the enum's order); a network game
                    // starts at Joining.
                    Repeater {
                        model: LoadingProgress.Meshes - LoadingProgress.Content + 1

                        LoadingStep {
                            id: row

                            required property int index

                            objectName: "loadingStep" + row.stage
                            visible: !screen.network || row.stage >= LoadingProgress.Joining
                            stage: LoadingProgress.Content + row.index
                            current: screen.progress.stage
                            text: screen.stepText(row.stage)
                            count: screen.stepCount(row.stage)
                        }
                    }
                }
            }
        }

        GameButton {
            objectName: "loadingCancelButton"
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Ui.screen.column
            large: true
            visible: screen.canCancel
            text: qsTr("Cancel")
            onClicked: screen.cancelRequested()
        }
    }
}
