import QtQuick
import QtQuick.Controls
import QtCraft

// The game's name as its logo, in the pixel font Pixelify Sans (SIL Open Font License 1.1, embedded
// with its licence in src/app/fonts; decision E-845, docs/UI-Leitfaden.md 4). The font is only for
// the logo; every other text is in the system font. Shrinks to its width rather than being cut.
Label {
    id: logo

    text: qsTr("QtCraft")
    font.family: pixelFont.name
    font.pixelSize: Ui.type.logo
    font.weight: Font.Normal
    color: Theme.text
    fontSizeMode: Text.HorizontalFit
    minimumPixelSize: Ui.type.title
    elide: Text.ElideRight
    Accessible.role: Accessible.Heading
    Accessible.name: text

    FontLoader {
        id: pixelFont

        source: "qrc:/qt/qml/QtCraft/fonts/PixelifySans.ttf"
    }
}
