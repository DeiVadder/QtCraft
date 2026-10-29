// SPDX-FileCopyrightText: 2026 the QtCraft authors
// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// One offer of the enchanting table (decision E-884), as in the reference: a field of parchment with
// the lapis it costs on the left, the clue in runes in the middle (as many letters readable as the
// bookshelves allow, QcRuneScript) and the levels it needs on the right. A click, Space or Enter
// takes it. A screen reader hears the levels, the price, how many letters are readable (the name once
// all are) and why an offer cannot be taken.
AbstractButton {
    id: offer

    // One map of QcEnchantingController::offers.
    required property var offerData
    readonly property bool exists: offerData.levelCost > 0
    readonly property string inkRole: enabled ? "ink" : "inkDisabled"

    // What a screen reader says of the offer.
    function spoken() {
        if (!exists) {
            return qsTr("No offer");
        }
        const name = offerData.readableLetters === offerData.letters
            ? qsTr("Level %1, %n lapis: %2", "", offerData.price).arg(offerData.levelCost).arg(offerData.clue)
            : qsTr("Level %1, %n lapis, %2 of %3 letters readable", "", offerData.price).arg(offerData.levelCost)
                .arg(offerData.readableLetters).arg(offerData.letters);
        if (offerData.levelsLacking && offerData.lapisLacking) {
            return qsTr("%1, not enough levels and lapis").arg(name);
        }
        if (offerData.levelsLacking) {
            return qsTr("%1, not enough levels").arg(name);
        }
        return offerData.lapisLacking ? qsTr("%1, not enough lapis").arg(name) : name;
    }

    enabled: offerData.affordable
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Ui.size.offerWidth
    implicitHeight: Ui.size.offerHeight
    padding: Ui.space.s
    // The focus outline reaches over the neighbours.
    z: visualFocus ? 1 : 0
    Accessible.role: Accessible.Button
    Accessible.name: spoken()
    Keys.onReturnPressed: click()
    Keys.onEnterPressed: click()

    background: ParchmentField {
        active: offer.exists && offer.enabled
        hovered: offer.hovered

        FocusOutline {
            visible: offer.visualFocus
        }
    }

    contentItem: RowLayout {
        spacing: Ui.space.s
        visible: offer.exists

        // The lapis it costs: the gem and how many.
        Item {
            Layout.preferredWidth: Ui.size.icon
            Layout.fillHeight: true

            Image {
                anchors.centerIn: parent
                width: Ui.size.icon
                height: Ui.size.icon
                source: ItemIcons.source("qtcraft:lapis_lazuli", ItemIcons.generation)
                smooth: false
                opacity: offer.enabled ? 1.0 : Ui.disabledOpacity
            }

            Label {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                text: offer.offerData.price
                color: Theme.text
                style: Text.Outline
                styleColor: Theme.textOutline
                font.family: Ui.pixelFont.name
                font.pixelSize: Ui.type.body
            }
        }

        // The clue: runes and readable letters on one line.
        Flow {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            clip: true

            // The size of a rune, which every sign takes as its height (one picture for all offers).
            PixelArt {
                id: runeSize
                visible: false
                picture: "rune/0/ink"
            }

            Repeater {
                model: offer.offerData.signs

                Item {
                    id: sign

                    required property var modelData
                    readonly property bool hidden: modelData.rune >= 0

                    // A rune keeps a pixel's gap to the next sign; letters bring their own.
                    width: hidden ? rune.width + Ui.size.artPixel : letter.implicitWidth
                    height: runeSize.height

                    PixelArt {
                        id: rune
                        visible: sign.hidden
                        // Readable letters ask for no rune picture.
                        picture: sign.hidden ? "rune/" + sign.modelData.rune + "/" + offer.inkRole : ""
                    }

                    Label {
                        id: letter
                        visible: !sign.hidden
                        anchors.bottom: parent.bottom
                        text: sign.modelData.text
                        color: offer.enabled ? Theme.ink : Theme.inkDisabled
                        font.family: Ui.pixelFont.name
                        font.pixelSize: Ui.type.section
                    }
                }
            }
        }

        LevelBadge {
            Layout.alignment: Qt.AlignVCenter
            text: offer.offerData.levelCost
            available: offer.enabled
            missing: !offer.enabled
            onParchment: true
        }
    }
}
