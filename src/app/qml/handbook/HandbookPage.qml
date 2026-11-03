// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The open page of the handbook (HandbookScreen): icon, title, category and pack, the text, the
// facts as a two-column table (the technical ones folded under "Technical") and the sections with
// their recipes and links; scrolls as a whole.
Flickable {
    id: view

    // The open page (QcHandbookModel::page()).
    property var page: ({})

    readonly property var facts: page.facts || []
    readonly property var technicalFacts: page.technicalFacts || []
    // The technical facts are unfolded; they fold again on the next page.
    property bool technicalShown: false

    // A link or a recipe cell asks for the page `target`.
    signal linkActivated(string target)

    clip: true
    contentWidth: width
    contentHeight: pageColumn.implicitHeight + Ui.space.xl
    boundsBehavior: Flickable.StopAtBounds
    onPageChanged: technicalShown = false

    ScrollBar.vertical: ThemedScrollBar {}

    ColumnLayout {
        id: pageColumn

        width: view.width - Ui.space.l
        spacing: Ui.space.m

        RowLayout {
            spacing: Ui.space.m

            Rectangle {
                Layout.preferredWidth: Ui.size.handbookPageIcon
                Layout.preferredHeight: Ui.size.handbookPageIcon
                visible: (view.page.icon || "").length > 0
                color: Theme.panelRaised
                radius: Ui.radius

                Image {
                    anchors.fill: parent
                    anchors.margins: Ui.space.s
                    source: parent.visible ? ItemIcons.source(view.page.icon, ItemIcons.generation) : ""
                    sourceSize: Qt.size(width, height)
                    smooth: false
                }
            }

            ColumnLayout {
                spacing: Ui.size.slotGap

                Label {
                    text: view.page.title || ""
                    color: Theme.text
                    font.pixelSize: Ui.type.title
                    font.bold: true
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Label {
                    text: (view.page.categoryTitle || "") + "  ·  " + (view.page.packName || "")
                    color: Theme.textMuted
                    font.pixelSize: Ui.type.help
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: view.page.text || ""
            color: Theme.text
            font.pixelSize: Ui.type.body
            wrapMode: Text.Wrap
            textFormat: Text.MarkdownText
        }

        HandbookFacts {
            objectName: "handbookFacts"
            Layout.fillWidth: true
            facts: view.facts
            onLinkActivated: target => view.linkActivated(target)
        }

        // The id, states and sound of the page under "Technical", folded (HB-3 of
        // docs/Menue-Pruefung.md).
        GameButton {
            objectName: "handbookTechnical"
            visible: view.technicalFacts.length > 0
            kind: GameButton.Quiet
            text: (view.technicalShown ? "▾ " : "▸ ") + qsTr("Technical")
            Accessible.name: qsTr("Technical")
            Accessible.role: Accessible.CheckBox
            Accessible.checked: view.technicalShown
            onClicked: view.technicalShown = !view.technicalShown
        }

        HandbookFacts {
            objectName: "handbookTechnicalFacts"
            Layout.fillWidth: true
            visible: view.technicalShown && facts.length > 0
            facts: view.technicalFacts
            onLinkActivated: target => view.linkActivated(target)
        }

        Repeater {
            model: view.page.sections || []

            ColumnLayout {
                id: sectionColumn

                required property var modelData

                Layout.fillWidth: true
                spacing: Ui.space.s

                Label {
                    visible: text.length > 0
                    text: sectionColumn.modelData.title
                    color: Theme.heading
                    font.pixelSize: Ui.type.section
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: sectionColumn.modelData.text
                    color: Theme.text
                    font.pixelSize: Ui.type.body
                    wrapMode: Text.Wrap
                    textFormat: Text.MarkdownText
                }

                Flow {
                    Layout.fillWidth: true
                    visible: sectionColumn.modelData.recipes.length > 0
                    spacing: Ui.space.l

                    Repeater {
                        model: sectionColumn.modelData.recipes

                        HandbookRecipe {
                            required property var modelData

                            recipe: modelData
                            onActivated: target => view.linkActivated(target)
                        }
                    }
                }

                Flow {
                    Layout.fillWidth: true
                    visible: sectionColumn.modelData.links.length > 0
                    spacing: Ui.space.s

                    Repeater {
                        model: sectionColumn.modelData.links

                        HandbookLink {
                            required property var modelData

                            link: modelData
                            onActivated: target => view.linkActivated(target)
                        }
                    }
                }
            }
        }
    }
}
