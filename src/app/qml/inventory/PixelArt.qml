import QtQuick
import QtCraft

// A picture of the workbench screens (QcPixelArtProvider, decision E-884): "book", "arrow/<role>",
// "rune/<index>/<role>" …, drawn at `pixel` screen pixels per art pixel without smoothing, so it
// stays as blocky as the textures. Its implicit size is the scaled one, so layouts keep it. A
// picture, not a control: screen readers skip it.
Item {
    id: art

    property string picture
    // Screen pixels per pixel of the art (a whole number keeps every art pixel the same size).
    property real pixel: Ui.size.artPixel

    implicitWidth: image.implicitWidth * pixel
    implicitHeight: image.implicitHeight * pixel
    width: implicitWidth
    height: implicitHeight
    Accessible.ignored: true

    Image {
        id: image

        anchors.fill: parent
        source: art.picture.length > 0 ? "image://pixelart/" + art.picture : ""
        smooth: false
        mipmap: false
    }
}
