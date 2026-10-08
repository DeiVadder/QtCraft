# Modding

QtCraft mods are **data-only content packs**: JSON definitions and PNG textures, no executable code. The base game itself is such a pack (`content/qtcraft`, embedded in the binary) and is loaded by the same code, so everything the base game defines a mod can add or replace.

## Where mods live

| Game | Directory |
|------|-----------|
| Default | `<app data>/mods` (macOS: `~/Library/Application Support/QtCraft/QtCraft/mods`) |
| `QTCRAFT_DATA_DIR=<dir>` | `<dir>/mods` |
| `--mods-dir <dir>` | `<dir>` (game and `qtcraft-headless`) |
| `--no-mods` | base game only |

Every sub-directory with a `pack.json` is one mod. The main menu's **Mods** screen lists all mods with their state and lets you switch them off; the choice is stored in `mods.json` in the mods directory (`{"disabled": ["mod_id"]}`) and applies when the next world starts. The control interface reports the loaded packs with `{"cmd":"mods"}`.

Try the example mod shipped in the repository:

```sh
build/debug/src/app/qtcraft.app/Contents/MacOS/qtcraft --mods-dir mods
```

## Manifest: `pack.json`

```json
{
    "id": "rubies",
    "name": "Rubies",
    "version": "1.0.0",
    "description": "Example mod: ruby ore, ruby tools, a ruby block and the crystal beetle.",
    "dependencies": [{"id": "qtcraft", "minVersion": "0.1.0"}]
}
```

| Field | Required | Meaning |
|-------|----------|---------|
| `id` | yes | Lower-case letters, digits, `_`, `-`, `.` (starting with a letter or digit, no `..`, at most 64 characters). Also the default namespace of the pack's names. Must be unique; `qtcraft` is the base game. |
| `version` | yes | Dotted version number (`1`, `1.2`, `1.2.3`); `1` and `1.0.0` count as the same version. |
| `name`, `description` | no | Shown in the Mods screen. |
| `dependencies` | no | Pack ids, either `"other_mod"` or `{"id": "other_mod", "minVersion": "1.2"}`. Up to 64. |

## Load order

1. The base game.
2. Every enabled mod, each after all of its dependencies. Among mods that are ready at the same time, the smaller id goes first, so the order is always the same.

A mod is **skipped** (and every mod that depends on it) when its manifest is broken, a dependency is missing, disabled, skipped or older than `minVersion`, dependencies form a cycle, or its id is already taken. The game still starts; the Mods screen and the log say why.

Inside a mod, broken files are skipped one by one with a warning (`qtcraft.game: Content: mod rubies: …` in the log, `content warning:` on stderr for `qtcraft-headless`); the rest of the mod still loads. After loading, references to unknown items, blocks or textures are reported as warnings; such recipes never match and such drops are never given.

## Content files

All files are optional. Names without a namespace get the pack id (`ruby` in pack `rubies` is `rubies:ruby`); write `qtcraft:stick` to refer to base content or `other_mod:thing` to refer to a dependency.

| Folder | One file per | Notes |
|--------|--------------|-------|
| `blocks/` | block | Every visible, non-liquid block automatically gets an item that places it. |
| `items/` | item | Tools, weapons, food. |
| `recipes/` | recipe | Shaped (`pattern`, `key`) or shapeless (`ingredients`), up to 3 × 3. |
| `mobs/` | creature | Stats, spawn rules, box model, drops. |
| `textures/` | texture | `*.json` procedural textures or `*.png` files (scaled to 16 × 16), named by file name. |
| `lang/` | language | `en.json`, `de.json`, …: translation key → text. |
| `biomes/`, `features/`, `ores/` | world-generation entry | Used by the server's terrain generator. |
| `worldgen/terrain.json` | – | Global terrain settings (replaces the earlier one). |

The fields of every definition type are listed in `docs/Architektur.md` (sections Content, Interaction, Crafting, Creatures). Display names are never stored in definitions: blocks, items and mobs use translation keys (`block.<namespace>.<name>`, `item.…`, `mob.…`, `biome.…` by default) that the `lang/` files fill. English (`en`) is the fallback.

### Replacing existing content

A definition whose name already exists **replaces the earlier one completely** (decision E-026), for example `blocks/glass.json` with `"name": "qtcraft:glass"`. Copy the original file and change what you need; fields you leave out get their defaults, not the old values. Runtime ids stay the same, so saved worlds are not affected. Translations are merged key by key.

## Worlds and mods

A saved world remembers the packs it was played with (`world.json`, `packs`). Opening it without one of them logs a warning; blocks of the missing mod become air and its items disappear from inventories. Adding a mod to an existing world is safe; new ores and biomes only appear in chunks generated afterwards.

## The example mod `mods/rubies`

| Kind | Entries |
|------|---------|
| Blocks | `ruby_ore` (needs an iron pickaxe, drops a ruby), `ruby_block` |
| Items | `ruby`, `ruby_sword`, `ruby_pickaxe` |
| Recipes | ruby block from 9 rubies (crafting table), 9 rubies from a block, sword and pickaxe (with `qtcraft:stick`) |
| World generation | ruby ore veins between y 4 and 28 |
| Mob | `crystal_beetle`: hostile, spawns on cave floors, drops a ruby |
| Textures | procedural (ore, gem, tools, beetle) |
| Translations | English and German |

Screenshot: `docs/screenshots/07-mod-items.png` (all mod items in the inventory, the ruby recipe on offer). `tests/test_mods.cpp` loads it, checks every part and crafts with its recipes; `tests/test_headless_cli.cpp` starts `qtcraft-headless --mods-dir mods`.

## Boundary between core and mods

Mods **can**: add and replace blocks, items, recipes, mobs, biomes, features, ores, terrain settings, textures and translations; depend on other mods.

Mods **cannot**: run code, add new kinds of behaviour (a new AI, a new block mechanic, new UI), change the protocol, the save format or the rules the server enforces, or read and write files outside their own pack directory. Everything a mod provides goes through `QcContentPackLoader` (the only code that reads pack files) into `QcContentRegistry`; the rest of the game only sees the registry. The server sends the registry to clients, so clients never read mod files themselves.

Only regular files and real directories are read. A linked mod folder is reported as not loaded; linked sub-folders and files and special files such as pipes or devices are not read (silently inside the content folders, with a warning for `pack.json` and `worldgen/terrain.json`). Size limits: 4 MiB per file, 256 mod directories (64 loaded), at most 64 drops per block, manifest `name` 256 and `version` 64 characters; the parser checks every field's type and range. All mods together may make the loader read at most 64 MiB: the files of a mod are measured before anything is read (each file counts at least 256 bytes, a hard link as often as it appears), and every attempt counts, also for mods that are refused afterwards or loaded a second time. Each mod is checked right after loading, exactly as its content would reach clients (including the items created for its blocks): the counts clients accept and the 32 MiB content sync. A mod that fails is refused and leaves no trace; at most 64 mods are loaded and checked per start (refused ones count too). Shaped recipes have at most 9 keys; procedural textures keep only `pattern`, `primary`, `secondary` (at most 32 characters each), `variation` and `seed`. A final read-back like a client's is the safety net: should it ever fail, the game starts without mods. World generation is limited to 256 ore definitions with at most 4,096 ore blocks per chunk in total, 256 biomes, 1,024 features and 4,096 feature placements (at most 64 per biome). PNG textures may be at most 128 × 128 pixels (checked before decoding). A mod that fails a check is skipped together with its dependents; the Mods screen shows the reason after the next world start. Security questions that go beyond that (sandboxing, signatures, permissions for future script hooks) are collected in `docs/Mod-Sicherheit.md`.
