# Playing QtCraft

How to start the game, the default controls, the settings and where the game keeps its files. The rules of the game itself (crafting, farming, Signal Dust, villages, creatures …) are in the in-game handbook (H or F1), also as [Markdown](handbook/index.md) ([German](handbook/de/index.md)). Building the game from source: [Building.md](Building.md).

## Starting the game

**macOS.** Double-click `QtCraft.app` (from `scripts/release.sh` or a release package), or start a build directly:

```sh
build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft
```

The app runs on any Mac with macOS 13.3 or later, Apple silicon or Intel. It is signed ad hoc only (`codesign -s -`); notarisation needs an Apple developer account. Another Mac therefore shows on the first start of a downloaded copy that “QtCraft” cannot be opened because Apple cannot check it for malicious software (macOS 15 and later: “QtCraft Not Opened”). To open it anyway: move the app to Applications, try to open it once, then in **System Settings → Privacy & Security** click **Open Anyway** next to the QtCraft message and confirm with your password; on macOS 13 and 14 right click (Control-click) the app in Finder, choose **Open** and confirm. macOS remembers the choice. Alternatively remove the download mark in Terminal: `xattr -dr com.apple.quarantine /Applications/QtCraft.app`. A copy without the download mark (for example built on that Mac) opens directly.

**Windows (x64).** The Windows package is a standalone zip: unpack it completely and double-click `qtcraft.exe`; no installation and no administrator rights. The program is not signed, so SmartScreen asks on the first start (“More info” → “Run anyway”). Requirements: Windows 10 or 11, 64 bit, a graphics card with Direct3D 11 or 12. The `README-Windows.txt` in the zip (German, with a short English section) has the details; how the zip is built is in [Building.md](Building.md#windows).

## Main menu

The main menu lists saved worlds, creates new ones (name, world type, optional seed, game mode, whether the inventory is kept on death) and opens multiplayer, the character screen (outfit, hair, colours, presets, random look, with a live preview of the character that can be turned, zoomed and animated), the handbook, mods and texture packs, and the settings. The menu shows your character on a profile card, the inventory shows it next to the place of the armour slots.

Game modes: survival, creative (fly, every block in the creative inventory, no damage), adventure (survival without breaking and placing except where the world allows it) and spectator (fly through blocks, unseen). World types: Normal (`terrain` on the command line), Flat, Void and Showcase (a world that shows every block, creature and item of the game and its mods).

Multiplayer (hosting a world for the LAN, joining a server, the dedicated server): [Multiplayer.md](Multiplayer.md).

## Controls

| Input (default) | Action |
|-------|--------|
| Mouse, arrow keys | Look around (click into the window to capture the mouse) |
| W A S D, Space, Shift, Ctrl (Option on a Mac) | Move, jump, sneak, sprint (press once while walking forward, or tap W twice; lasts while W is held) |
| Left mouse / F | Mine; left click on a creature attacks it |
| Right mouse / R | Place the held block (stairs, slabs, torches, doors … turn and attach by where you look and click), eat held food when hungry, till grass or dirt with a hoe, plant seeds, carrots and potatoes on farmland, feed animals (pigs: carrots or potatoes, sheep: wheat) so they get young ones, open a crafting table, furnace or chest, open and close doors, trapdoors and gates (sneak to place against them); with an empty bucket take water or lava, with a full one pour it; with an empty glass bottle catch a firefly |
| Middle mouse / B | Pick block: put the item of the block in view into your hand (selects it in the hotbar or swaps it in; in creative mode taken from the catalogue) |
| Q, Ctrl+Q (Control+Q on a Mac) | Throw one item or the whole stack of the selected slot (walk over items to pick them up) |
| V or F5 | Switch the view: first person, third person behind, third person in front |
| 1–9, mouse wheel | Select a hotbar slot |
| G (hold) | Radial menu of the hotbar: choose a slot with the mouse |
| E | Inventory with 2 × 2 crafting grid and recipe list; in creative mode the creative inventory (every block and item incl. mods, tabs, search, bin) |
| Space twice (creative) | Fly or stop flying; Space rises, Shift sinks, sprint doubles the speed, landing ends the flight |
| Ladders | Walk into the wall behind a ladder or jump to climb, sneak to hold on |
| Furnace screen | Something to smelt goes on top, fuel below, the result appears on the right; eat when the food bar right of the hearts runs low |
| Mine lamp | Clay (4, crafting table) → unfired lamp shell → furnace → fired shell + firefly in a bottle (fireflies glow over swamps at night) → lamp for floors, walls and ceilings |
| H or F1 | Handbook: every block, item, recipe, creature and biome with its values and links, plus chapters on playing (opens at the block in view; in the inventory, furnace and chests at the item under the mouse, or click the small ? on a slot) |
| T or Enter, / | Chat; `/` opens it with a command started (`/help`) |
| Tab (hold) | Player list |
| L | Labels in the world on and off |
| P or F6 | Hide the HUD (photo mode) |
| K or F2 | Screenshot |
| F11 | Fullscreen on and off |
| I or F3 | Debug display: off, compact (frame rate, frame time), full (position, light, target, time per subsystem with graphs) |
| O or F4 | Debug options: chunk borders, hitboxes, light values, creature goals, remesh flash |
| Esc | Pause menu (settings, save, quit to menu); closes the topmost screen first |

Every key except Esc can be changed under Settings → Controls (click a key, press the new one; Esc cancels, Backspace removes it, a key used by another action is swapped). The keys are stored in `settings.ini`; *Reset to defaults* restores the table above.

**Gamepads** (Xbox, PlayStation, Switch Pro and most others) work together with keyboard and mouse; their buttons can be changed under Settings → Controller. The handbook chapter “Controls” lists the default buttons.

**Mac keyboards.** F1–F12 control brightness, Mission Control, search and other system features, so macOS keeps them from the game; that is why every default key is a letter or another normal key, and the function keys are only second keys. To use them, hold fn or switch on “Use F1, F2, etc. keys as standard function keys” in System Settings → Keyboard → Keyboard Shortcuts → Function Keys. No default key uses Cmd or Control there, because macOS keeps Cmd+Q, Cmd+H, Cmd+Space, Cmd+Tab, Control+arrow keys and Control+Space for itself: sprint is Option, the whole stack is thrown with Control+Q, and Cmd cannot be bound at all. Putting an action on Control is allowed, but Control with the arrow keys or Space then switches desktops or the input source. Keys pressed together with Cmd do nothing in the game, so Cmd+Q quits (saving the world), Cmd+H hides and Cmd+W closes the window. Option+E, U, N and I are dead keys only while typing text; in the world they work as sprint plus that key.

## Settings

The settings are grouped in pages, in this order: Graphics (view distance, field of view and its widening when sprinting, camera shake, texture filtering, window, borderless or fullscreen on a chosen screen, frame rate limits in the world and in menus, VSync), Audio (volumes of all sound, effects, music and ambience plus a mute switch), Controls (mouse sensitivity, invert mouse, keys), Controller (gamepad), Interface (language, interface size, view mode, labels in the world, hiding the hand with the HUD, pausing when the window loses focus), Texture packs ([Texturpakete.md](Texturpakete.md)) and Advanced (debug display and its visualisations, log file and crash reports, “Open log folder”). The character's look and the saved character presets are kept with them. Everything is stored in `settings.ini` in the data directory. The UI is available in English and German.

## Data directory

Saves, settings, logs and mods are kept in the data directory of the game:

| System | Data directory |
|---|---|
| macOS | `~/Library/Application Support/QtCraft/QtCraft` |
| Windows | `%APPDATA%\QtCraft\QtCraft` |
| Linux | `~/.local/share/QtCraft/QtCraft` |

`QTCRAFT_DATA_DIR` points the game somewhere else, for example to keep test runs away from your real worlds. The dedicated server uses a data directory of its own ([Multiplayer.md](Multiplayer.md)).

## Mods

Mods are data-only content packs in `<data directory>/mods`. The release app copies the example mod to that folder on the first start, switched off – turn it on in the Mods screen. From a build, try the example mod of this repository with:

```sh
build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft --mods-dir mods
```

See [Modding.md](Modding.md) for the format and the boundary between core and mods.
