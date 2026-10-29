// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The open page of the handbook (HandbookScreen): icon, title, category and pack, the text, the
// facts as a two-column table and the sections with their recipes and links; scrolls as a whole.
Flickable {
    id: view

    // The open page (QcHandbookModel::page()).
    property var page: ({})

    // A link or a recipe cell asks for the page `target`.
    signal linkActivated(string target)

    clip: true
    contentWidth: width
    contentHeight: pageColumn.implicitHeight + Ui.px(20)
    boundsBehavior: Flickable.StopAtBounds

    ScrollBar.vertical: ThemedScrollBar {}

    ColumnLayout {
        id: pageColumn

        width: view.width - Ui.px(16)
        spacing: Ui.px(12)

        RowLayout {
            spacing: Ui.px(12)

            Rectangle {
                Layout.preferredWidth: Ui.px(56)
                Layout.preferredHeight: Ui.px(56)
                visible: (view.page.icon || "").length > 0
                color: Theme.panelRaised
                radius: Ui.px(3)

                Image {
                    anchors.fill: parent
                    anchors.margins: Ui.px(6)
                    source: parent.visible ? ItemIcons.source(view.page.icon, ItemIcons.generation) : ""
                    sourceSize: Qt.size(44, 44)
                    smooth: false
                }
            }

            ColumnLayout {
                spacing: Ui.px(2)

                Label {
                    text: view.page.title || ""
                    color: Theme.text
                    font.pixelSize: Ui.px(26)
                    font.bold: true
                }

                Label {
                    text: (view.page.categoryTitle || "") + "  ·  " + (view.page.packName || "")
                    color: Theme.textMuted
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: view.page.text || ""
            color: Theme.text
            font.pixelSize: Ui.px(15)
            wrapMode: Text.Wrap
            textFormat: Text.MarkdownText
        }

        // Facts as a two-column table.
        Rectangle {
            Layout.fillWidth: true
            visible: factRepeater.count > 0
            implicitHeight: factColumn.implicitHeight + Ui.px(12)
            color: Theme.rowHighlight
            radius: Ui.px(4)

            ColumnLayout {
                id: factColumn

                x: Ui.px(8)
                y: Ui.px(6)
                width: parent.width - Ui.px(16)
                spacing: Ui.px(4)

                Repeater {
                    id: factRepeater

                    model: view.page.facts || []

                    RowLayout {
                        id: factRow

                        required property var modelData

                        Layout.fillWidth: true
                        spacing: Ui.px(10)

                        Label {
                            Layout.preferredWidth: Ui.px(210)
                            Layout.alignment: Qt.AlignTop
                            topPadding: Ui.px(4)
                            text: factRow.modelData.label
                            color: Theme.textMuted
                            wrapMode: Text.Wrap
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Ui.px(6)

                            Label {
                                visible: text.length > 0
                                topPadding: Ui.px(4)
                                width: Math.min(implicitWidth, parent.width)
                                text: factRow.modelData.value
                                color: Theme.text
                                wrapMode: Text.Wrap
                            }

                            Repeater {
                                model: factRow.modelData.links

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

        Repeater {
            model: view.page.sections || []

            ColumnLayout {
                id: sectionColumn

                required property var modelData

                Layout.fillWidth: true
                spacing: Ui.px(6)

                Label {
                    visible: text.length > 0
                    text: sectionColumn.modelData.title
                    color: Theme.heading
                    font.pixelSize: Ui.px(18)
                    font.bold: true
                }

                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: sectionColumn.modelData.text
                    color: Theme.text
                    font.pixelSize: Ui.px(15)
                    wrapMode: Text.Wrap
                    textFormat: Text.MarkdownText
                }

                Flow {
                    Layout.fillWidth: true
                    visible: sectionColumn.modelData.recipes.length > 0
                    spacing: Ui.px(18)

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
                    spacing: Ui.px(6)

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
