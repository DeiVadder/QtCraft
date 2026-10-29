pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtCraft

// The main menu's page for a new world (variant C, decision E-852; draft D.3 A of
// docs/Menue-Pruefung.md, decision E-930): on the left the world (name, type with its seed and the
// die, or the layers), the rules (game mode, keeping the inventory) and for a normal world its world
// options (their preset or changes, and "Customise …" to their page, decision E-917); on the right a
// small map preview (decision E-921). The groups scroll as a whole; "‹ Back" and "Create and play"
// stay in the footer. Esc and the pad's B go back (MainMenu).
ColumnLayout {
    id: page

    required property GameController game

    // The page should be shown (open(), also the test hook "@<ms> open:newWorldPage").
    // The type and mode the form starts with (indexes into their lists; the test hooks
    // QTCRAFT_MENU_WORLD_TYPE and QTCRAFT_MENU_GAME_MODE set them).
    property int startWorldType: 0
    property int startGameMode: 0
    // The seed of a world whose seed field stays empty, drawn when the form opens, so that the map
    // preview shows the world that will be created (decision E-921).
    property string randomSeed: ""
    // A normal world: it has world options and a map preview.
    readonly property bool terrain: worldType.currentValue === "terrain"

    signal openRequested
    signal backRequested
    // "Customise …": the page of the world generation options (WorldOptionsPage).
    signal optionsRequested

    // Shows the form with its defaults, the name chosen for typing over it.
    function open() {
        worldName.text = qsTr("New World");
        worldType.currentIndex = startWorldType;
        seed.text = "";
        drawSeed();
        flatLayers.text = "";
        gameMode.currentIndex = startGameMode;
        keepInventory.checked = true;
        page.game.worldOptions.reset();
        page.game.worldOptions.preview.resetView();
        form.contentY = 0;
        openRequested();
        worldName.forceActiveFocus(Qt.TabFocusReason);
        worldName.selectAll();
    }

    // A new random seed for an empty seed field.
    function drawSeed() {
        randomSeed = String(Math.floor(Math.random() * 2147483647));
    }

    // The die: a new random seed in the field, which the map then shows.
    function rollSeed() {
        drawSeed();
        seed.text = randomSeed;
    }

    // The seed the world gets: the field's text, else the random one.
    function worldSeed(): string {
        const seedText = seed.text.trim();
        return seedText.length > 0 ? seedText : randomSeed;
    }

    function create() {
        page.game.createWorld(worldName.text, page.worldSeed(), {
            keepInventory: keepInventory.checked,
            worldType: worldType.currentValue,
            layers: worldType.currentValue === "flat" ? flatLayers.text : "",
            gameMode: gameMode.currentValue,
            worldOptions: page.game.worldOptions.values
        });
    }

    // What the world generation options say in a line: their preset, or how many differ.
    function optionsSummary(): string {
        const options = page.game.worldOptions;
        if (options.changedCount === 0)
            return qsTr("Every option at its default.");
        const preset = options.presets.find(each => each.name === options.preset);
        return preset ? qsTr("Preset: %1").arg(preset.title) : qsTr("%n option(s) changed.", "", options.changedCount);
    }

    // Back from the options: the small map shows the default view again (the options' map may
    // have moved or zoomed), and the button that opened them has the focus again.
    function backFromOptions() {
        page.game.worldOptions.preview.resetView();
        moreOptions.forceActiveFocus(Qt.TabFocusReason);
    }

    // Whether the keyboard's focus is in `item` (the control of a setting row is a child of it).
    function holdsFocus(item: Item): bool {
        for (let at = Window.activeFocusItem; at; at = at.parent) {
            if (at === item)
                return true;
        }
        return false;
    }

    // The pad's down from the form's last setting goes to "Create and play": the footer lies too
    // far to the side of it for the spatial step (the map column has no focus target).
    function gamepadCommand(command: string): bool {
        if (command !== "down" || !page.holdsFocus(page.terrain ? moreOptions : keepInventory))
            return false;
        createButton.forceActiveFocus(Qt.TabFocusReason);
        return true;
    }

    // A die showing five, the symbol of the random seed's button.
    component DieIcon: Rectangle {
        id: die

        readonly property real pip: Math.max(2 * Ui.size.line, Math.round(width / 5))

        implicitWidth: Ui.size.icon
        implicitHeight: Ui.size.icon
        radius: Ui.radius
        color: Theme.text
        border.color: Theme.knobOutline
        border.width: Ui.size.line

        Repeater {
            model: [Qt.point(0.27, 0.27), Qt.point(0.73, 0.27), Qt.point(0.5, 0.5), Qt.point(0.27, 0.73), Qt.point(0.73, 0.73)]

            delegate: Rectangle {
                required property point modelData

                x: modelData.x * die.width - width / 2
                y: modelData.y * die.height - height / 2
                width: die.pip
                height: die.pip
                color: Theme.knobOutline
            }
        }
    }

    spacing: 0

    Binding {
        target: page.game.worldOptions.preview
        property: "seedText"
        value: page.worldSeed()
    }

    SettingsPage {
        id: form

        objectName: "newWorldForm"
        Layout.fillWidth: true
        Layout.fillHeight: true
        title: qsTr("New world")

        // Draft D.3 A of docs/Menue-Pruefung.md: world, rules and world options on the left, the
        // small map on the right; one column where the page is too narrow for both, the map then at
        // the end, below the world options.
        GridLayout {
            id: columns

            readonly property real mapColumn: Ui.size.map + 2 * Ui.space.l
            readonly property bool beside: page.terrain && width >= Ui.screen.s + mapColumn + columnSpacing

            objectName: "newWorldColumns"
            Layout.fillWidth: true
            columns: beside ? 2 : 1
            columnSpacing: Ui.space.xl
            rowSpacing: Ui.space.xl

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Ui.space.xl

                SettingGroup {
                    title: qsTr("World")

                    SettingRow {
                        label: qsTr("Name")

                        GameTextField {
                            id: worldName

                            objectName: "worldName"
                            Layout.fillWidth: true
                            Accessible.name: qsTr("World name")
                            text: qsTr("New World")
                            onAccepted: page.create()
                        }
                    }
                    SettingDropdown {
                        id: worldType

                        objectName: "worldTypeChoice"
                        label: qsTr("World type")
                        help: worldType.currentValue === "flat" ? qsTr("The same layers everywhere, no caves or trees, no creatures of their own accord.")
                              : worldType.currentValue === "void" ? qsTr("Nothing but a small platform; noon, no creatures of their own accord.")
                              : worldType.currentValue === "showcase" ? qsTr("A flat world with every block, creature and item on show, at noon.")
                              : qsTr("Hills, mountains, caves, oceans and biomes.")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { value: "terrain", text: qsTr("Normal") },
                            { value: "flat", text: qsTr("Flat") },
                            { value: "void", text: qsTr("Void") },
                            { value: "showcase", text: qsTr("Showcase") }
                        ]
                    }
                    // The field shows the random seed as its placeholder while it is empty, so the
                    // map always shows the seed the field stands for; the die draws a new one.
                    SettingRow {
                        visible: page.terrain
                        label: qsTr("Seed")
                        help: qsTr("A number or a text; empty for a random one.")

                        GameTextField {
                            id: seed

                            objectName: "worldSeed"
                            Layout.fillWidth: true
                            Accessible.name: qsTr("Seed")
                            placeholderText: page.randomSeed
                            onAccepted: page.create()
                        }
                        GameButton {
                            id: dice

                            objectName: "worldSeedRandom"
                            // As high as the field: the pad's down from the type reaches the field.
                            implicitHeight: seed.implicitHeight
                            leftPadding: Ui.space.s + Ui.size.icon + Ui.space.s
                            text: qsTr("Roll")
                            Accessible.name: qsTr("Random seed")
                            onClicked: page.rollSeed()

                            DieIcon {
                                x: Ui.space.s
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            ThemedToolTip {
                                text: qsTr("Draws a new random seed.")
                                shown: dice.hovered
                            }
                        }
                    }
                    SettingRow {
                        visible: worldType.currentValue === "flat"
                        label: qsTr("Layers")
                        help: qsTr("From the bottom up; empty for the default.")

                        GameTextField {
                            id: flatLayers

                            Layout.fillWidth: true
                            Accessible.name: qsTr("Layers from the bottom up")
                            placeholderText: visible ? page.game.defaultFlatLayers() : ""
                            onAccepted: page.create()
                        }
                    }
                }

                SettingGroup {
                    title: qsTr("Rules")

                    SettingDropdown {
                        id: gameMode

                        objectName: "gameModeChoice"
                        label: qsTr("Game mode")
                        help: gameMode.currentValue === "creative"
                              ? qsTr("Every block and item without limit, flying, instant breaking, no damage or hunger.")
                              : gameMode.currentValue === "adventure"
                              ? qsTr("Survival for prepared maps: blocks break and items are placed only where /adventure allows it.")
                              : gameMode.currentValue === "spectator"
                              ? qsTr("Fly through blocks and look around, unseen; nothing can be touched.")
                              : qsTr("Gather, craft and survive: health, hunger and creatures count.")
                        textRole: "text"
                        valueRole: "value"
                        model: [
                            { value: "survival", text: qsTr("Survival") },
                            { value: "creative", text: qsTr("Creative") },
                            { value: "adventure", text: qsTr("Adventure") },
                            { value: "spectator", text: qsTr("Spectator") }
                        ]
                    }
                    SettingSwitch {
                        id: keepInventory

                        objectName: "keepInventory"
                        label: qsTr("Keep inventory on death")
                        checked: true
                    }
                }

                SettingGroup {
                    visible: page.terrain
                    title: qsTr("World generation")

                    SettingRow {
                        label: qsTr("World options")
                        help: page.optionsSummary()

                        // At the start of the control column like the switch above it, so the
                        // pad's down reaches it from there.
                        GameButton {
                            id: moreOptions

                            objectName: "moreWorldOptions"
                            text: qsTr("Customise …")
                            Accessible.name: qsTr("Customise the world options")
                            onClicked: page.optionsRequested()
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                    }
                }
            }

            // The small map: biomes, sea and rivers with the most common biomes; zoom, the full
            // legend and the villages are on the world options page.
            MapPreviewGroup {
                objectName: "newWorldMap"
                Layout.preferredWidth: columns.beside ? columns.mapColumn : -1
                Layout.fillWidth: !columns.beside
                Layout.alignment: Qt.AlignTop
                visible: page.terrain
                compact: true
                preview: page.game.worldOptions.preview
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Ui.size.line
        color: Theme.separator
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.topMargin: Ui.space.l
        spacing: Ui.space.s

        GameButton {
            kind: GameButton.Quiet
            text: qsTr("‹ Back")
            Accessible.name: qsTr("Back to the start page")
            onClicked: page.backRequested()
        }
        Item {
            Layout.fillWidth: true
        }
        GameButton {
            id: createButton

            objectName: "createWorldButton"
            kind: GameButton.Primary
            large: true
            text: qsTr("Create and play")
            onClicked: page.create()
        }
    }
}
