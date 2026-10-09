# Modding

QtCraft mods are **data-only content packs**: JSON definitions (including synthesized sounds) and PNG textures, no executable code. The base game itself is such a pack (`content/qtcraft`, embedded in the binary) and is loaded by the same code, so everything the base game defines a mod can add or replace.

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
| `shapes/` | block shape | Boxes, crossed planes, collision, conditions on states, turning; see below. |
| `parts/` | building-part family | Stairs, slabs, walls … made from every material of a kind; see below. |
| `materials/` | material or exclusion | Base block and kind of a material, or blocks that never get parts. |
| `items/` | item | Tools, weapons, food. |
| `recipes/` | recipe | Shaped (`pattern`, `key`) or shapeless (`ingredients`), up to 3 × 3. An ingredient `#tag` accepts every item with that tag (`#qtcraft:planks`, `#qtcraft:coals`). |
| `smelting/` | smelting recipe | One input item becomes a result in a machine of the recipe's `category`. |
| `mobs/` | creature | Stats, spawn rules, box model or skeleton with outfit, drops. |
| `models/` | skeleton | Bones with roles, pivots and slotted boxes. |
| `appearance/` | outfit or hair style | Textures and tints per slot; hair boxes. |
| `sounds/` | sound | Synthesis parameters; see below. |
| `textures/` | texture | `*.json` procedural textures or `*.png` files (scaled to 16 × 16), named by file name. |
| `lang/` | language | `en.json`, `de.json`, …: translation key → text. |
| `biomes/`, `features/`, `ores/` | world-generation entry | Used by the server's terrain generator. |
| `worldgen/terrain.json` | – | Global terrain settings (replaces the earlier one). |

Blocks can also define plants: `"shape": "cross"` draws two crossed quads (non-solid cutout by default), a drop can carry a `"chance"` (0–1, default 1), and a `"sapling"` object turns a block into a sapling that grows into a tree or column feature:

```json
{
    "name": "ruby_sapling",
    "shape": "cross",
    "hardness": 0,
    "sapling": {
        "feature": "ruby_tree",
        "minSeconds": 90,
        "maxSeconds": 240,
        "plantOn": ["qtcraft:grass_block", "qtcraft:dirt"],
        "plantChance": 0.75
    },
    "textures": { "all": "ruby_sapling" }
}
```

`plantOn` lists the blocks it may stand on (empty: any solid block), `plantChance` is the chance that a dropped sapling item plants itself when it despawns. Leaves of tree features decay once no trunk of a tree feature is within four blocks; give them a drop with a chance (e.g. the sapling, 0.08) to let forests regrow. The procedural texture pattern `sapling` draws a small plant.

Furnaces and other machines come from data too. A block with a `machine` object opens a screen with an input, a fuel and an output slot; `smelting/*.json` says what a machine of that category makes, and `fuelSeconds` on items or blocks says how long they burn (an item created for a block inherits the block's value):

```json
{ "name": "kiln", "hardness": 3, "tool": "pickaxe", "toolLevel": 1,
  "machine": { "category": "firing", "speed": 2.0 },
  "textures": { "all": "kiln_side", "north": "kiln_front" } }
```

```json
{ "name": "fired_clay", "category": "firing", "input": "qtcraft:clay",
  "result": { "item": "fired_clay", "count": 1 }, "seconds": 8 }
```

| Field | Meaning |
|-------|---------|
| `machine.category` | Recipe category the machine works on (default `smelting`, the base furnace). Plain names without namespace (lower-case letters, digits, `_`, `-`, `.`, at most 64 characters), so a mod can add recipes to the base furnace by using `smelting`. |
| `machine.speed` | 0.01–100 (default 1): seconds of recipe work per second; fuel burns at the same rate. A block cannot be a crafting table and a machine. |
| `smelting`: `input`, `result` (`item`, `count` 1–64), `seconds` (0.05–3600, default 10), `category` | Per category and input the first recipe in load order counts; a later one is reported as a warning. To change a base recipe, reuse its name. |
| `fuelSeconds` (items and blocks) | 0–100000, default 0 (no fuel). Base values: coal and charcoal 80, wood 15, wooden tools 10, sticks and saplings 5. |
| `food` (items) | Food points restored when eaten, whole points up to 20 (larger values fill the bar). |

A machine lights one fuel item only when its input has a recipe and the result fits into the output; machines work only while their chunk is loaded, and their contents fall out when the block is broken. The server limits a world to 4096 machines.

### Shapes, states and building parts

Blocks can be any set of boxes (decision E-045). A shape is named in `shapes/` (or written inline as the block's `shape` object); the base game has `cube`, `cross`, `cactus`, `stairs`, `slab`, `fence`, `fence_gate`, `wall`, `pane`, `door`, `trapdoor`, `torch`, `ladder`, `chest` and `facing_cube`. Unqualified shape names refer to the pack's own shapes, except `cube` and `cross`, which always mean the base ones; write `qtcraft:stairs` to use a base shape.

```json
{ "name": "pillar", "rotate": "facing", "flip": "half",
  "parts": [
    { "boxes": [[0.25, 0, 0.25, 0.75, 1, 0.75]] },
    { "when": { "open": true }, "boxes": [[0, 0, 0, 1, 0.125, 1]], "collision": [], "texture": "lid", "uv": "box" }
  ] }
```

| Field | Meaning |
|-------|---------|
| `boxes`, `crosses` | `[x0, y0, z0, x1, y1, z1]` in metres within the cell (0..1). Crosses are two diagonal planes (plants) and never collide. At most 32 parts with 16 boxes each and 64 boxes, crosses and collision boxes per shape together. All shape variants of all blocks together may derive at most 131,072 boxes (states that turn, flip and select the same parts share one variant); a block or shape beyond that budget is refused with a message. |
| `when` | Property → value or list of values; the part applies when all hold. |
| `collision` | Boxes for bodies instead of `boxes` (up to 1.5 m high, `[]` for none). |
| `texture`, `uv` | Texture slot of the block for these boxes (`textures.slots`), `uv: "box"` stretches the whole texture over each face (torches) instead of cutting the part where the face lies. |
| `rotate`, `flip` | Shapes are drawn for a block facing north; the property in `rotate` (values `north`, `east`, `south`, `west`) turns them, the one in `flip` mirrors them upside down when its value is `top`. Face textures turn with `rotate`. |

Block states (decision E-046) are declared per block: `"states": { "facing": ["north", "east", "south", "west"], "open": [false, true] }` (the first value is the default; at most 8 properties of 16 values and 1024 states per block). The server derives these properties when a block is placed or its neighbours change: `facing` (`"facing": "away"` from the player, or `"toward"` for fronts), `half` and `type` (`bottom`/`top`, slabs add `double`), `attach` (`floor`, `ceiling`, `north`, … towards the clicked block), `hinge`, `north`/`east`/`south`/`west` and `up` for blocks with `connects` (`{"group": "fence", "to": ["fence"]}`: connect to members of these groups and full solid blocks). Further flags: `support` (`below` or `attached`: breaks and drops without it), `toggles` (using flips `open`), `door` (two blocks tall), `climbable`, `light` (0–15), `container` (`{"slots": 27}`, a chest), `tags` (of the block's item) and `itemTexture` (a flat icon instead of the block model). Worlds store states by name (`qtcraft:oak_stairs[facing=east,half=bottom]`), so reordering values or adding properties keeps old worlds; dropped properties fall back to defaults with a warning.

Building parts (decision E-048) come from two kinds of files. A family in `parts/` describes one part for all materials of the kinds it lists; the block, item, recipe and names of every part are generated after all packs are loaded, so families and materials of different packs combine:

```json
{ "name": "pillar", "kinds": ["stone"], "suffix": "pillar", "hardness": 1.0, "fuel": 1.0,
  "block": { "shape": "pillar", "lightOpacity": 0 },
  "recipe": { "pattern": ["#", "#", "#"], "key": { "#": "@" }, "count": 3 },
  "translations": { "en": "%1 pillar", "de": "%1säule" } }
```

```json
{ "name": "ruby", "block": "ruby_block", "kind": "stone", "exclude": ["qtcraft:wall"] }
{ "name": "excluded", "excluded": true, "blocks": ["ruby_ore"] }
```

The part `<material namespace>:<material>_<suffix>` copies textures, tool, tool level, sound material and fuel (× `fuel`) and hardness (× `hardness`) from the base block and drops itself; `@` in the recipe stands for the base block; `%1` in a translation is the material's name (translation key `material.<namespace>.<name>`, override with `translation`). Base families: `stairs`, `slab` (wood and stone), `wall` (stone), `fence`, `fence_gate` (wood). Entries of `exclude` are family names; a material can leave out families of other packs by their full name (`qtcraft:wall`). Base families also include `carpet` (kind `wool`). Base materials: `oak`, `birch`, `spruce` (planks), `cobblestone`, `stone` (no wall), `brick`, `sandstone`, `stone_bricks`, `polished_stone` (no wall), and the eight wool colours (`white_wool` … `purple_wool`, carpets only). Nothing is made for excluded materials: blocks listed by an exclusion entry (base list in `materials/excluded.json`: ores, sand, gravel, clay, dirt, grass, snow, ice, logs, leaves, saplings, cactus, glass, water, bedrock, crafting table, furnace, chest) and every block that is no plain building block (not an opaque solid full cube without states, a machine, crafting table or chest, unbreakable or glowing), the latter with a warning. A block defined explicitly under a part's name is never replaced, nor is a recipe a pack defines under that name. Recipes take named ingredients before tags, so a tag never uses up an item a named ingredient needs.

### Liquids, falling blocks, plants and creatures (stage 14)

Liquids flow when they have a `flow` object (decision E-054). They need the state `level` with the values `"0"` (source) up to the flow distance in order, and may have `falling` (`[false, true]`):

```json
{ "name": "molten_ruby", "liquid": true, "solid": false, "replaceable": true, "render": "cutout", "light": 10,
  "contactDamage": 1, "drops": [],
  "states": { "level": ["0", "1", "2", "3", "4"], "falling": [false, true] },
  "flow": { "distance": 4, "ticks": 15, "movement": 0.6, "bucket": "molten_ruby_bucket", "sound": "qtcraft:liquid.lava.pop",
            "reactions": [{ "with": "qtcraft:water", "source": "qtcraft:obsidian", "flowing": "ruby_sand" }] },
  "textures": { "all": "molten_ruby" } }
```

| Field | Meaning |
|-------|---------|
| `flow.distance` | 1–15 blocks a flow reaches sideways from a source (water 7, lava 3). |
| `flow.ticks` | 1–1200 server ticks (20 per second) between flow steps (water 5, lava 30). |
| `flow.infinite` | Two sources next to each other over solid ground make a new source (water). |
| `flow.movement` | 0.05–2: factor on swimming speed inside the liquid (lava 0.45). |
| `flow.bucket` | Item an item with `scoops` (the empty bucket) becomes when it takes a source; without it the liquid cannot be taken. |
| `flow.reactions` | Touching the liquid `with`, a source of this liquid turns into `source`, a flowing part into `flowing` (lava next to water: obsidian or cobblestone). |
| `flow.sound` | Played now and then where the liquid spreads. |
| `contactDamage` (any block) | Damage per hit to players and creatures touching the block, every half second at most; items in it burn. |
| `washable` (any block) | A liquid flowing in breaks the block with its drops; default for `cross` shapes, set on the base torch (ladders and the mine lamp hold the liquid back). |

A bucket is an item with `"places": "<liquid>"` and `"emptiesTo": "qtcraft:bucket"`; using it pours a source and leaves the empty item in the hand. Burnt as fuel (`fuelSeconds`, the lava bucket) it leaves the empty item in the fuel slot, where it may stay or be taken out (nothing else can be put next to it); fuel with `emptiesTo` must have `"maxStack": 1`, otherwise the item is rejected. `"scoops": true` makes an item (the empty bucket) aim at liquids and take sources.

Blocks with `"falls": true` (solid blocks only) fall as entities while the cell below is air, liquid or replaceable, and become blocks again where they land, or drop as items when that cell is taken (a torch) or the block would end up inside a player or creature. Plants use `"support": "below"` (default for `cross`) with `"ground": [block names]` (instead of any full solid top face) and `"groundLiquid": "qtcraft:water"` (the liquid must touch the ground block from a side, unless the ground is the plant itself, as for sugar cane). World generation places single blocks with features of `"type": "plant"` (`"block"`, `"ground"`) and, with `"nearWater": true`, only on ground at sea level with water next to it (sugar cane uses a `column` feature with it).

Features of `"type": "pool"` sink a basin of a liquid (`"block"`, e.g. `qtcraft:lava`) into cave floors. They are not listed by biomes (a biome naming one gets a warning) but tried in every chunk: `attempts` (1–8) tries, each taken with `chance` (0–1), look for a cave floor in a random column at or below a random height between `minY` and `maxY`; the basin reaches up to `radius` (1–6) blocks from that column and `depth` (1–4) blocks down, narrowing with depth. Cells on the chunk's edge and every cell whose sides or bottom would not hold liquid (air, liquids, non-solid or replaceable blocks) are left out, and a pool not open to the cave above is not placed; cells under another liquid are left out too, and the rock hollowed over a pool keeps any block that holds back another liquid or carries a falling block, so generated pools never flow or meet other liquids. The base game places `qtcraft:lava_pool` (y 5–40, two attempts at 0.3, radius 3, depth 2); the example mod adds `rubies:molten_ruby_pool`. All mods together may define at most 64 pool attempts per chunk.

Creatures may fly (`"flying": true`: no gravity, 1.2–3 m above the ground), glow (`"glow": 0–15`: drawn that bright, pulsing; it lights nothing around them), belong to the ambient category (`"spawn": {"ambient": true}`, own cap of 10 per player, gone at daybreak when they spawn at night), be limited per kind (`"spawn": {"maxCount": 8}`), spawn only in some biomes (`"spawn": {"biomes": ["qtcraft:swamp"]}`) and be caught: `"catch": {"with": "qtcraft:glass_bottle", "item": "qtcraft:firefly_bottle"}` turns one held `with` item into `item` when the player uses it on the creature.

Further procedural texture patterns: `lava`, `flower`, `tall_grass`, `mushroom`, `sugar_cane`, `bottle` and `bucket` (contents in the secondary colour, transparent `#00000000` for empty), `lantern` (a lamp housing with a glowing window, for `uv: "box"`).

Characters come from data as well: `models/*.json` defines skeletons (bones with a role, a pivot and boxes with slot names), `appearance/*.json` outfits (a texture and an optional tint per slot) and hair styles (extra boxes on the head). A creature can use `"skeleton": "qtcraft:humanoid", "outfit": "mymod:robe"` instead of `model` boxes and is then animated like players (walking, attacking, hit, death; `"armsForward": true` for zombie-like arms). New outfits and hair styles appear in the character screen of players who have the mod; `"playable": false` keeps a creature outfit out of that list, and the server refuses it for players. Tinted textures should be light (near white); the player's colour is multiplied in.

### Sounds

Sounds are not audio files but synthesis parameters; the game renders them when a world starts (decision E-040). A sound has up to 8 layers; each is a tone, noise or crackle with an envelope and simple filters:

```json
{
    "name": "block.crystal.break",
    "volume": 0.8,
    "variants": 3,
    "pitchVariation": 0.1,
    "layers": [
        { "wave": "sine", "frequency": 3500, "endFrequency": 3200, "duration": 0.4, "attack": 0.001, "curve": 2.5, "gain": 0.5 },
        { "wave": "crackle", "density": 1500, "highpass": 2500, "duration": 0.3, "curve": 2, "gain": 0.6 }
    ]
}
```

| Field | Meaning |
|-------|---------|
| `bus` | `effects` (default), `ambient` or `music`: which volume slider applies. |
| `volume` | 0–1 (default 1). Every sound is normalised to the same peak; `volume` sets its loudness, layer `gain`s (0–1) only weigh layers against each other. |
| `variants` | 1–8 differently seeded renderings, one picked per play; `pitchVariation` (0–0.5) changes the pitch randomly by up to that share. |
| `loop` | Rendered seamless and played until stopped (the ambience loops `qtcraft:ambient.wind`, `.cave`, `.water`). |
| `range` | 1–128 m (default 16): beyond it a positioned sound of volume 1 is not heard. |
| layer `wave` | `sine`, `square`, `triangle`, `saw`, `noise` (default) or `crackle` (sparse clicks, `density` 1–5000 per second). |
| layer `frequency`, `endFrequency` | 20–20000 Hz (default 440); `endFrequency` sweeps exponentially to that value (0: none). |
| layer `start`, `duration` | Seconds (0–10, 0.001–10, default 0 and 0.2). |
| layer `attack`, `curve` | Linear rise in seconds (at most `duration`), then decay `(1 - t)^curve` (0–8, default 2; 0 holds the level). |
| layer `lowpass`, `highpass` | Filter frequencies in Hz (20–20000; 0 or missing: off). |
| layer `tremoloRate`, `tremoloDepth` | Volume wobble: 0–50 Hz and 0–1. |

Blocks name a sound **material**: `"sound": "stone"` makes the block use `<namespace>:block.<material>.<action>` for the actions `break`, `place`, `step` (walking, jumping, landing) and `hit` (mining strikes). Base materials are `qtcraft:stone`, `wood`, `dirt`, `gravel`, `sand`, `grass`, `snow` and `glass`; a mod block writes `"sound": "qtcraft:wood"`, a mod's own material `"sound": "crystal"` needs the four sounds `block.crystal.break` etc. in the mod. Without `sound` a block gets wood (tool `axe`), dirt (`shovel`), grass (cross shape or `cutout`), glass (`translucent`) or stone; liquids and invisible blocks are silent. `"sounds": {"hit": "chime"}` replaces single actions. Creatures have `"sounds": {"ambient": …, "hurt": …, "death": …}` (default: no ambient sound, `qtcraft:mob.generic.hurt` and `.death`), machines `"machine": {"sound": …}` (default `qtcraft:machine.furnace`, heard while fuel burns). A definition with the name of a base sound replaces it, e.g. `"name": "qtcraft:block.stone.break"`. Names that no pack defines are reported as warnings and stay silent. `qtcraft-headless --export-sounds <dir> --mods-dir <mods>` writes all sounds as WAV files to listen to them.

The fields of every definition type are listed in `docs/Architektur.md` (sections Content, Interaction, Crafting, Creatures). Display names are never stored in definitions: blocks, items and mobs use translation keys (`block.<namespace>.<name>`, `item.…`, `mob.…`, `biome.…` by default) that the `lang/` files fill. English (`en`) is the fallback.

### Replacing existing content

A definition whose name already exists **replaces the earlier one completely** (decision E-026), for example `blocks/glass.json` with `"name": "qtcraft:glass"`. Copy the original file and change what you need; fields you leave out get their defaults, not the old values. Runtime ids stay the same, so saved worlds are not affected. Translations are merged key by key.

## Worlds and mods

A saved world remembers the packs it was played with (`world.json`, `packs`). Opening it without one of them logs a warning; blocks of the missing mod become air and its items disappear from inventories. Adding a mod to an existing world is safe; new ores and biomes only appear in chunks generated afterwards.

## The example mod `mods/rubies`

| Kind | Entries |
|------|---------|
| Blocks | `ruby_ore` (needs an iron pickaxe, drops a ruby), `ruby_block`, `ruby_lantern` (inline shape, light 11, needs a block below) |
| Building parts | material `ruby` (stairs and slab, walls left out), family `pillar` with its shape (also for the base stones, e.g. `qtcraft:cobblestone_pillar`), `ruby_ore` excluded |
| Items | `ruby`, `ruby_sword`, `ruby_pickaxe` |
| Recipes | ruby block from 9 rubies (crafting table), 9 rubies from a block, sword and pickaxe (with `qtcraft:stick`), lantern (ruby over a torch) |
| Smelting and fuel | `ruby_from_gravel`: gravel becomes a ruby in the base furnace (30 s); `ember_shard` (dropped by the beetle) burns 120 s |
| World generation | ruby ore veins between y 4 and 28 |
| Mob | `crystal_beetle`: hostile, spawns on cave floors, drops a ruby |
| Liquid | `molten_ruby`: flows 4 blocks every 15 ticks, light 10, burns; its bucket `molten_ruby_bucket` (bucket + ruby block) empties into `qtcraft:bucket`; next to water a source becomes obsidian, a flow ruby sand; rare pools of it in cave floors (`molten_ruby_pool`, y 5–28) |
| Plant | `ruby_rose`: grows on grass, faint light 4; the mod replaces `qtcraft:plains` with a copy that adds the `ruby_rose` feature (mods replace whole definitions) |
| Falling block | `ruby_sand` (4 sand + ruby) falls like sand |
| Sounds | the material `crystal` of the ruby block (break, place, step, hit) and the beetle's ambient, hurt and death sounds |
| Textures | procedural (ore, gem, tools, beetle) |
| Translations | English and German |

Screenshot: `docs/screenshots/07-mod-items.png` (all mod items in the inventory, the ruby recipe on offer). `tests/test_mods.cpp` loads it, checks every part and crafts with its recipes; `tests/test_headless_cli.cpp` starts `qtcraft-headless --mods-dir mods`.

## Boundary between core and mods

Mods **can**: add and replace blocks (with states, shapes and light), shapes, building-part families and materials (and exclude materials from parts), items with tags, recipes, smelting recipes, fuels, machine blocks with their own categories, mobs, biomes, features, ores, terrain settings, textures, sounds and translations; depend on other mods.

Mods **cannot**: run code, add new kinds of behaviour (a new AI, a new block mechanic, new UI: every machine uses the furnace logic and screen, every storage block the chest screen, state rules are the fixed ones above), change the protocol, the save format or the rules the server enforces, or read and write files outside their own pack directory. Everything a mod provides goes through `QcContentPackLoader` (the only code that reads pack files) into `QcContentRegistry`; the rest of the game only sees the registry. The server sends the registry to clients, so clients never read mod files themselves.

Only regular files and real directories are read. A linked mod folder is reported as not loaded; linked sub-folders and files and special files such as pipes or devices are not read (silently inside the content folders, with a warning for `pack.json` and `worldgen/terrain.json`). Size limits: 4 MiB per file, 256 mod directories (64 loaded), at most 64 drops per block, at most 64 `plantOn` entries per sapling, manifest `name` 256 and `version` 64 characters; the parser checks every field's type and range. All mods together may make the loader read at most 64 MiB: the files of a mod are measured before anything is read (each file counts at least 256 bytes, a hard link as often as it appears), and every attempt counts, also for mods that are refused afterwards or loaded a second time. Each mod is checked right after loading, exactly as its content would reach clients (including the items created for its blocks): the counts clients accept and the 32 MiB content sync. A mod that fails is refused and leaves no trace; at most 64 mods are loaded and checked per start (refused ones count too). Shaped recipes have at most 9 keys; procedural textures keep only `pattern`, `primary`, `secondary` (at most 32 characters each), `variation` and `seed`. A final read-back like a client's is the safety net: should it ever fail, the game starts without mods. World generation is limited to 256 ore definitions with at most 4,096 ore blocks per chunk in total, 256 biomes, 1,024 features and 4,096 feature placements (at most 64 per biome) and 64 pool attempts per chunk. PNG textures may be at most 128 × 128 pixels (checked before decoding). Sounds are limited to the ranges above (8 layers of at most 10 s, 8 variants), checked by the parser and again by every client; a client renders at most 16 Mi samples (64 MiB) of sound, sounds beyond that stay silent. A mod that fails a check is skipped together with its dependents; the Mods screen shows the reason after the next world start. Security questions that go beyond that (sandboxing, signatures, permissions for future script hooks) are collected in `docs/Mod-Sicherheit.md`.
