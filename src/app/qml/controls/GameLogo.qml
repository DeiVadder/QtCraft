import QtQuick
import QtQuick.Controls
import QtCraft

// The game's name as its logo, in the pixel font Pixelify Sans (SIL Open Font License 1.1, embedded
// with its licence in src/app/fonts, loaded once as Ui.pixelFont; decision E-845, docs/UI-Leitfaden.md
// 4). Besides the logo only the workbench screens use it (decision E-884); every other text is in
// the system font. Shrinks to its width rather than being cut.
Label {
    id: logo

    text: qsTr("QtCraft")
    font.family: Ui.pixelFont.name
    font.pixelSize: Ui.type.logo
    font.weight: Font.Normal
    color: Theme.text
    fontSizeMode: Text.HorizontalFit
    minimumPixelSize: Ui.type.title
    elide: Text.ElideRight
    Accessible.role: Accessible.Heading
    Accessible.name: text
}
