pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCraft

// The map preview of a new world (decision E-921): the biomes, heights, sea and rivers the seed and
// the world options make, from the world's own generator, with a legend of the biomes on it. The
// map fills the height of the page's view and grows to the right, the legend beside it (below it
// where the page is narrow). Drag or the arrow keys move the view, the wheel, "+" and "−" zoom;
// villages and structures show only with their switch, so the map does not give them away, and
// then every kind has its own marker and a check box in the legend. Used on "New world" and "More
// world options"; GameController.worldOptions.preview computes it.
SettingGroup {
    id: group

    required property WorldMapPreview preview
    // The seed field is empty: the map shows the random seed the world will get, and another one
    // can be drawn.
    property bool randomSeed: false
    // The page's view: the map takes its height, less the rows below it, and the page scrolls to
    // the map when it gets the keyboard's focus.
    property Flickable flickable: null

    signal anotherSeedRequested

    // A legend entry's share as the legend shows it ("34 %", "< 1 %").
    function shareText(percent: int): string {
        return percent < 1 ? qsTr("< 1 %") : qsTr("%1 %").arg(percent);
    }

    // Scrolls the page so that the group starts at its top (the map focused by the keyboard is as
    // high as the page's view).
    function scrollIntoView() {
        if (!group.flickable)
            return;
        const top = group.mapToItem(group.flickable.contentItem, 0, 0).y;
        group.flickable.contentY = Math.max(0, Math.min(top, group.flickable.contentHeight - group.flickable.height));
    }

    title: qsTr("Map preview")

    // The marker of a kind of site (WorldMapPreview.siteLegend): its tint, one of four colours,
    // and its shape, each with a dark outline; the village (style 0) a red square with a white one.
    component SiteMarker: Item {
        id: marker

        property int style: 0
        property int shape: WorldMapPreview.Square
        property int tint: 0
        readonly property bool diamond: shape === WorldMapPreview.Diamond
        readonly property bool ring: shape === WorldMapPreview.Ring
        readonly property color fill: [Theme.danger, Theme.text, Theme.padYellow, Theme.padPink][tint]

        implicitWidth: Ui.space.m
        implicitHeight: Ui.space.m

        Rectangle {
            anchors.centerIn: parent
            width: marker.diamond ? parent.width * 0.75 : parent.width
            height: width
            rotation: marker.diamond ? 45 : 0
            radius: marker.shape === WorldMapPreview.Circle || marker.ring ? width / 2 : 0
            color: marker.ring ? Theme.knobOutline : marker.fill
            border.color: marker.style === 0 ? Theme.text : marker.ring ? marker.fill : Theme.knobOutline
            border.width: marker.style === 0 || marker.ring ? Math.max(2 * Ui.size.line, width / 4) : Ui.size.line
        }
    }

    GridLayout {
        id: body

        readonly property real legendWidth: Ui.size.map
        // The legend beside the map while the page is wide enough for both.
        readonly property bool beside: width >= 2 * legendWidth + columnSpacing
        readonly property real widthForMap: beside ? width - legendWidth - columnSpacing : width
        readonly property real heightForMap: (group.flickable ? group.flickable.height : 0) - controls.implicitHeight - sitesSwitch.implicitHeight - focusInfo.implicitHeight - 5 * Ui.space.l
        readonly property real mapSide: Math.max(Ui.size.map, Math.min(widthForMap, heightForMap))

        Layout.fillWidth: true
        Layout.topMargin: Ui.space.s
        columns: beside ? 2 : 1
        columnSpacing: Ui.space.l
        rowSpacing: Ui.space.m

        ColumnLayout {
            Layout.alignment: Qt.AlignTop
            spacing: Ui.space.xs

            Rectangle {
                id: frame

                // A drag's shift that stays until the moved map arrives.
                property point shift: Qt.point(0, 0)
                // Markers under the pointer: their own tooltip shows instead of the map's.
                property int hoveredMarkers: 0

                objectName: "worldMapFrame"
                Layout.preferredWidth: body.mapSide
                Layout.preferredHeight: body.mapSide
                color: Theme.field
                clip: true
                activeFocusOnTab: true
                onActiveFocusChanged: if (activeFocus)
                    group.scrollIntoView()
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Map of the new world, %1 by %1 blocks").arg(2 * group.preview.radius)
                Keys.onLeftPressed: group.preview.pan(-0.25, 0)
                Keys.onRightPressed: group.preview.pan(0.25, 0)
                Keys.onUpPressed: group.preview.pan(0, -0.25)
                Keys.onDownPressed: group.preview.pan(0, 0.25)
                Keys.onPressed: event => {
                    if (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal) {
                        group.preview.zoomIn();
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Minus) {
                        group.preview.zoomOut();
                        event.accepted = true;
                    }
                }

                Item {
                    id: shownMap

                    x: frame.shift.x + drag.activeTranslation.x
                    y: frame.shift.y + drag.activeTranslation.y
                    width: frame.width
                    height: frame.height

                    Image {
                        objectName: "worldMapImage"
                        anchors.fill: parent
                        source: group.preview.source
                        // Every revision is a new image: Qt's cache would only keep the old ones alive.
                        cache: false
                        smooth: false
                        fillMode: Image.Stretch
                        onStatusChanged: if (status === Image.Ready)
                            frame.shift = Qt.point(0, 0)
                    }

                    // The square searched for villages and structures, when the view is larger.
                    Rectangle {
                        objectName: "worldMapSitesArea"
                        visible: group.preview.showSites && group.preview.radius > group.preview.sitesRadius
                        anchors.centerIn: parent
                        width: parent.width * group.preview.sitesRadius / group.preview.radius
                        height: width
                        color: Theme.clear
                        border.color: Theme.text
                        border.width: Ui.size.line
                        opacity: 0.7
                    }

                    // Kept inside the map at its edges; faded while newer sites are planned; the kind's
                    // name on hover.
                    Repeater {
                        model: group.preview.sites

                        delegate: SiteMarker {
                            id: site

                            required property var modelData

                            style: modelData.style
                            shape: modelData.shape
                            tint: modelData.tint
                            x: Math.max(0, Math.min(frame.width - width, modelData.x * frame.width - width / 2))
                            y: Math.max(0, Math.min(frame.height - height, modelData.y * frame.height - height / 2))
                            opacity: group.preview.busy ? 0.5 : 1

                            HoverHandler {
                                id: hover

                                onHoveredChanged: frame.hoveredMarkers += hovered ? 1 : -1
                            }
                            Component.onDestruction: if (hover.hovered)
                                frame.hoveredMarkers -= 1
                            ThemedToolTip {
                                text: site.modelData.text
                                shown: hover.hovered
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: group.preview.stage === WorldMapPreview.Empty
                    text: qsTr("Drawing the map …")
                    color: Theme.textMuted
                    font.pixelSize: Ui.type.help
                }

                Label {
                    objectName: "worldMapRefining"
                    anchors.left: parent.left
                    anchors.bottom: parent.bottom
                    visible: group.preview.busy && group.preview.stage !== WorldMapPreview.Empty
                    padding: Ui.space.xs
                    text: qsTr("Refining …")
                    color: Theme.text
                    font.pixelSize: Ui.type.help
                    background: Rectangle {
                        color: Theme.scrim
                    }
                }

                DragHandler {
                    id: drag

                    target: null
                    onActiveChanged: {
                        if (active)
                            return;
                        frame.shift = Qt.point(frame.shift.x + activeTranslation.x, frame.shift.y + activeTranslation.y);
                        group.preview.pan(-activeTranslation.x / frame.width, -activeTranslation.y / frame.height);
                    }
                }

                WheelHandler {
                    onWheel: event => {
                        if (event.angleDelta.y > 0)
                            group.preview.zoomIn();
                        else if (event.angleDelta.y < 0)
                            group.preview.zoomOut();
                    }
                }

                // The biome and block column under the pointer, from the cells of the shown map.
                HoverHandler {
                    id: mapHover
                }
                Item {
                    x: mapHover.point.position.x
                    y: mapHover.point.position.y + Ui.space.l

                    ThemedToolTip {
                        objectName: "worldMapTip"
                        shown: mapHover.hovered && frame.hoveredMarkers === 0 && text.length > 0
                        text: group.preview.source.length > 0
                              ? group.preview.placeAt((mapHover.point.position.x - shownMap.x) / frame.width, (mapHover.point.position.y - shownMap.y) / frame.height).text ?? ""
                              : ""
                    }
                }

                // The point the line under the map describes while the map has the keyboard's focus.
                Rectangle {
                    visible: frame.activeFocus
                    anchors.centerIn: parent
                    width: Ui.space.l
                    height: 2 * Ui.size.line
                    color: Theme.text
                    border.color: Theme.knobOutline
                    border.width: Ui.size.line / 2
                }
                Rectangle {
                    visible: frame.activeFocus
                    anchors.centerIn: parent
                    width: 2 * Ui.size.line
                    height: Ui.space.l
                    color: Theme.text
                    border.color: Theme.knobOutline
                    border.width: Ui.size.line / 2
                }

                FocusOutline {
                    visible: frame.activeFocus
                }
            }

            // What the centre of the map is, for the keyboard and the pad (no pointer there).
            Label {
                id: focusInfo

                objectName: "worldMapFocusInfo"
                Layout.preferredWidth: body.mapSide
                opacity: frame.activeFocus ? 1 : 0
                text: group.preview.centreText
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            objectName: "worldMapLegend"
            Layout.fillWidth: true
            Layout.minimumWidth: body.beside ? body.legendWidth : 0
            Layout.alignment: Qt.AlignTop
            spacing: Ui.space.xs

            Repeater {
                model: group.preview.legend

                delegate: RowLayout {
                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Ui.space.s

                    Rectangle {
                        Layout.preferredWidth: Ui.size.icon
                        Layout.preferredHeight: Ui.size.icon
                        color: parent.modelData.colour
                        border.color: Theme.knobOutline
                        border.width: Ui.size.line
                    }
                    Label {
                        Layout.fillWidth: true
                        text: parent.modelData.name
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: Ui.type.help
                        elide: Text.ElideRight
                    }
                    Label {
                        text: group.shareText(parent.modelData.percent)
                        color: Theme.textMuted
                        font.pixelSize: Ui.type.help
                    }
                }
            }

            // The kinds of villages and structures on the map, each shown or hidden by its box.
            Label {
                Layout.topMargin: Ui.space.s
                visible: group.preview.showSites && group.preview.siteLegend.length > 0
                text: qsTr("Villages and structures")
                color: Theme.heading
                font.pixelSize: Ui.type.help
                font.bold: true
            }

            Repeater {
                model: group.preview.showSites ? group.preview.siteLegend : []

                delegate: RowLayout {
                    id: kind

                    required property var modelData

                    Layout.fillWidth: true
                    spacing: Ui.space.s

                    SiteMarker {
                        Layout.preferredWidth: Ui.space.m
                        Layout.preferredHeight: Ui.space.m
                        Layout.leftMargin: (Ui.size.icon - Ui.space.m) / 2
                        Layout.rightMargin: (Ui.size.icon - Ui.space.m) / 2
                        style: kind.modelData.style
                        shape: kind.modelData.shape
                        tint: kind.modelData.tint
                    }
                    SettingCheck {
                        objectName: "worldMapKind_" + kind.modelData.name
                        text: qsTr("%1 (%2)").arg(kind.modelData.title).arg(kind.modelData.count)
                        Accessible.name: kind.modelData.title
                        font.pixelSize: Ui.type.help
                        checked: kind.modelData.shown
                        onToggled: group.preview.setKindShown(kind.modelData.name, checked)
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: group.preview.showSites && group.preview.radius > group.preview.sitesRadius
                text: qsTr("Villages and structures only inside the frame (%1 × %1 blocks).").arg(2 * group.preview.sitesRadius)
                color: Theme.textMuted
                font.pixelSize: Ui.type.help
                wrapMode: Text.WordWrap
            }
        }
    }

    RowLayout {
        id: controls

        Layout.fillWidth: true
        Layout.topMargin: Ui.space.xs
        spacing: Ui.space.s

        GameButton {
            objectName: "worldMapZoomOut"
            text: "−"
            Accessible.name: qsTr("Zoom out")
            enabled: group.preview.canZoomOut
            onClicked: group.preview.zoomOut()
        }
        GameButton {
            objectName: "worldMapZoomIn"
            text: "+"
            Accessible.name: qsTr("Zoom in")
            enabled: group.preview.canZoomIn
            onClicked: group.preview.zoomIn()
        }
        GameButton {
            text: qsTr("Centre")
            Accessible.name: qsTr("Centre the map on the world's middle")
            onClicked: group.preview.resetView()
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("%1 × %1 blocks").arg(2 * group.preview.radius)
            color: Theme.textMuted
            font.pixelSize: Ui.type.help
            elide: Text.ElideRight
        }
        GameButton {
            objectName: "worldMapAnotherSeed"
            visible: group.randomSeed
            text: qsTr("Another seed")
            onClicked: group.anotherSeedRequested()
        }
    }

    SettingSwitch {
        id: sitesSwitch

        objectName: "worldMapSites"
        label: qsTr("Show villages and structures")
        help: qsTr("Off by default, so the map keeps them a surprise.")
        checked: group.preview.showSites
        onToggled: group.preview.showSites = checked
    }
}
