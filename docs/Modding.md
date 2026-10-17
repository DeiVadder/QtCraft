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
| `appearance/` | outfit, hair style or cape | Textures and tints per slot; hair boxes; cape shape. |
| `sounds/` | sound | Synthesis parameters; see below. |
| `textures/` | texture | `*.json` procedural textures or `*.png` files (at most 128 × 128, scaled to the atlas tile size), named by file name. |
| `lang/` | language | `en.json`, `de.json`, …: translation key → text. The language is the file name; a suffix `.handbook` is left out, so `de.handbook.json` adds German texts as well (other dots stay part of the language). |
| `handbook/` | handbook chapter | A chapter of the handbook's "Playing" category; see below. |
| `biomes/`, `features/`, `ores/` | world-generation entry | Used by the server's terrain generator. |
| `trades/` | set of offers of a profession | What villagers of a profession buy and sell (stage 37.2c); see "Trading". |
| `commands/` | command macro | A chat and console command made of other commands; see below. |
| `worldgen/terrain.json` | – | Global terrain settings (replaces the earlier one). |
| `structures/` | structure | Buildings from block layers with creatures, armour stands, contents, labels; a `showcase` entry makes it a demo of the showcase world; see below. |
| `villages/` | village style | The pieces, roads, lamp posts and block swaps of the villages of some biomes (E-403); see Villages below. |
| `worldgen/showcase.json` | – | Settings of the showcase world: ground, time of day, podium blocks, block categories (replaces the earlier one). |
| `worldgen/villages.json` | – | Where villages go, which creature lives in them and which golems guard them (E-403, E-421; a later pack changes the fields it gives); see Villages below. |
| `villagers/` | villager species | How the villagers of a creature kind live (E-411, E-421, server side only): daily routine, default look, fleeing, trading, breeding; see Villagers below. |
| `professions/` | profession | A villager profession with its workstation blocks, outfit layers and hat (E-421, server side only); see Villagers below. |
| `worldgen/flat.json`, `worldgen/void.json` | – | Flat world: default layer `preset` (`"bedrock,59*stone,3*dirt,grass_block"`); void world: `platform`, `platformRadius`, `platformY`; both `timeOfDay`, `daylightCycle`, `mobSpawning` (E-223; replace the earlier ones). |
| `weather.json` | – | What the weather does (decision E-396; replaces the earlier one, missing keys keep the defaults; server side only): `lightning` with `meanSeconds` (10: one strike per player on average every that many seconds of a thunderstorm, 0.5–3600), `damage` (5, 0–100, players through their armour), `radius` (3, 0–8: blocks to each side and below the struck cell, six above), `burnSeconds` (8, 0–60: the struck bodies burn), `sparks` (2, 0–8: fires a strike may light at and right around the struck cell, each a spark with all limits of fire and none while fire does not spread); `snow` with `layer` (`snow_layer`: the block snow leaves on top of opaque blocks with a full top face under the open sky; it must be a block of this or an earlier pack that is not opaque, no liquid and has `"support": "below"`, else the whole file is refused with a warning, decision E-397) and `columnsPerTick` (4, 0–64: random columns within 48 blocks of each player looked at per tick while it snows). The values in the handbook are those of the base game. |

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

A plant that grows upwards by itself (like sugar cane) gets a `"growth"` object: `maxHeight` (2–64, default 3) is the tallest column it reaches, `averageSeconds` (1–86400, default 600) the average time between two growth steps of one column. It needs `"support": "below"`. Growth runs on random ticks of loaded sections (three random blocks per section and tick, so each block is ticked about every 68 s); a ticked top block adds a block of its kind above when the block above is air, the column is shorter than `maxHeight` and the column's base still has its `ground` and `groundLiquid`. Example: `"growth": { "maxHeight": 4, "averageSeconds": 300 }`. Server side only (not sent to clients); the handbook shows it on the block page.

Farming (stage 27, decisions E-289 to E-294; all server side only):

- `"crop"` makes a block ripen in stages on the same random ticks: `property` (default `age`; a state property with at least two values, the last one is ripe), `averageSeconds` (1–86400, default 1200, from the first to the ripe stage on wet soil), `dryFactor` (0–1, default 0.5, pace on dry soil or other ground), `minLight` (0–15, default 9), `plantChance` (0–1, default 0: a dropped item placing this crop that rests on free soil plants one of its stack with this chance once a second). With `fruit` (a block), `fruitSeconds` (default 300) and `fruitGround` (block names) a ripe crop is a stem that puts its fruit onto a free neighbour with that ground, one at a time. Crops need `"support": "below"` (usually `"ground": ["farmland"]`), must not be solid and cannot also have `growth`. Draw the stages with a shape whose parts are conditioned on the stage; a part's `texture` slot also applies to its `crosses`.
- `"soil"` makes a block farmland: `property` (default `moist`, last value wet, first dry), `water` (default `qtcraft:water`), `reach` (0–8, default 4), `revertsTo` (required, another block).
- `"tillsInto": "farmland"` on a block and `"tills": true` on an item (the hoes) turn the block into that soil when the item is used on it.
- `"flattensInto": "dirt_path"` on a block and `"flattens": true` on an item (the shovels, decision E-406) turn the block into that path when the item is used on its top with air above, wearing the item by one (not in creative mode). `"coveredInto": "dirt"` on a block turns it into that block at its next block update while an opaque full block lies on it (the dirt path).
- The base world grows wheat, carrots and potatoes only in the fields of villages (decision E-406; the style's `crops`); the plant features `wild_carrots` and `wild_potatoes` stay in the content for mods (a biome lists them under `features`) and later oases, but no base biome places them. Tall grass still drops wheat seeds.
- `"ignites": true` on an item (flint and steel, decision E-393) lights fire in the cell in front of the clicked face where fire may stand (a spark with the limits of fire), wearing the item by one per fire (except in creative mode); with `"dispense": "ignite"` a dispenser does the same in front of it. Such an item neither places, tills, scoops nor bottles.
- `"fertilizes": true` on an item (bone meal, decision E-426): used on a crop that is not ripe it grows 2–5 stages at once, on a sapling it lets the tree grow on the next tick with a chance of 45 % (with room for it); either takes one item, except in creative mode. Such an item neither places nor ignites. Dispensers throw it (no `dispense` action for it yet).
- **Launchers and ammunition (decisions E-425, E-427):** `"launcher": {"drawSeconds": 1.0, "speed": 60.0, "minPower": 0.1, "ammunition": ["#arrows"]}` makes an item a launcher (the bow): held with the use button down it draws for up to `drawSeconds` (0.05–10), released it shoots the first ammunition of the inventory it takes at `speed` m/s (1–200) times the power of the draw, `(f² + 2f) / 3` of the share `f` of `drawSeconds`; below `minPower` (0–1) nothing is shot. `ammunition` lists what it takes: item names and item tags as `#tag` (up to 64; unqualified names take the pack's namespace, so the bow's `#arrows` is `#qtcraft:arrows`). Without the list a launcher takes every ammunition item that is not `throwable`; an empty list (`[]`) means the same and is reported with a warning. A mod's new arrow carries `"tags": ["qtcraft:arrows"]` and fits the bow. Every shot uses one ammunition item and wears the launcher by one (`durability`), except in creative mode, where it also shoots without ammunition (the projectile of the item of the first projectile kind it takes). A launcher neither places, is eaten nor worn. `"projectile": "<name>"` makes an item ammunition (the arrow): the projectile kind it becomes when shot or thrown.
- **Throwing by hand (decision E-427):** `"throwable": {"speed": 30.0, "cooldownSeconds": 0.25, "inaccuracyDegrees": 1.0}` together with `projectile` makes an item thrown with the use button (the snowball): one item at once at `speed` m/s (1–200) along the view, turned at random by up to `inaccuracyDegrees` (0–45); the player can throw that item again after `cooldownSeconds` (0–60). Each throw uses one item, except in creative mode. A throwable item neither launches, places, is eaten nor worn; `throwable` without a known projectile is dropped with a warning, and with `ignites`, `fertilizes` or a bucket or bottle use those uses are dropped with a warning (the use button throws it). Throws count against the server's rate of use actions. Signal Dispensers do not throw projectiles yet (they drop the item like any other).
- **Projectile kinds** (`projectiles/<name>.json`, decision E-425, server side only): `name`, `item` (required: the ammunition it is shot from, drawn as and given back when picked up), `gravity` (m/s², default 20; the snowball 12 like the reference's thrown items), `drag` and `waterDrag` (share of the speed lost per second as an exponential rate, default 0.2 and 10), `damage` (health points per 20 m/s of speed, rounded up, default 2: a full bow shot deals 6; only the default of the damage effect, see `onHit`), `size` (edge of the box that hits bodies, default 0.5 m), `despawnSeconds` (stuck, default 60), `flightSeconds` (default 60), `sticks` (default true; a projectile that does not stick goes when it hits a block), `hitSound`, `stickSound`, `onHit`. Armour takes its share of the damage; a projectile that meets a body during its short invulnerability bounces back, unless it breaks.
- **Hit effects** (`onHit`, decision E-427, server side only): a list of up to 16 effects that together make one hit when the projectile hits a body, each `{"type": …}` with the type's own number; their order does not matter (the damage is dealt once, the strongest knockback and the longest burning count). Without `onHit` a kind has `[{"type": "damage", "amount": <damage>}, {"type": "knockback"}]` (the arrow). Types: `damage` (`amount` 0–1000, default the kind's `damage`: health points per 20 m/s of speed, rounded up; the body is not pushed unless a knockback effect says so), `knockback` (`strength` 0–10, default 1: pushes the body along the flight, 1 like a blow, also without damage; the strength scales the push, the lift grows only up to that of a blow; a hit without damage gives the body no invulnerability), `ignite` (`seconds` 0–3600, default 5: the body burns at least that long, as from fire; creative players do not), `break` (the projectile goes at any hit on a body or a block instead of bouncing or sticking; `sound` plays where it breaks). Example, the snowball: `"onHit": [{"type": "knockback"}, {"type": "break", "sound": "block.snow.break"}]`. A type the game does not know is left out with a warning when the content is loaded, the rest of the list stays. New types are code: a row in `QcHitEffect::types` and a handler in `QcHitEffects`. Planned: status effects (poison, from 52.2), spawning a creature (the egg), teleporting the shooter (the ender pearl, 52.3) and explosions.
- A drop may carry `"when": {"age": "7"}` (like shape conditions): it only drops from those states.
- A block drop may carry `"maxCount"` (0–64): it drops between `count` and `maxCount` items, chosen evenly (lapis lazuli ore: 4 to 9; decision E-398).
- **Experience (decision E-398, server side only):** `"experience"` is a whole number of points or `[min, max]` (0–100000) for an even choice. On a block, mining it with a tool that gets its drops gives that much as experience orbs (ores: coal `[0, 2]`, diamond `[3, 7]`, lapis lazuli `[2, 5]`, Signal Dust ore `[1, 5]`); on a mob, a player killing it does (zombie 5, pig and sheep `[1, 3]`); `breeding.experience` is what a pair gives when a young one is born (default `[1, 7]`). Smelting recipes take `"experience"` per item smelted (0–100, may be fractional, e.g. 0.7 for iron); the furnace gathers it until a player takes from the output, a fraction becomes one more point with its chance.
- A peaceful creature may carry `"breeding": {"food": ["carrot"], "loveSeconds": 30, "cooldownSeconds": 300, "growUpSeconds": 1200, "partnerDistance": 8}` (the food list also reaches clients).

```json
{
    "name": "ruby_berries",
    "shape": "qtcraft:crop",
    "states": { "age": ["0", "1", "2", "3", "4", "5", "6", "7"] },
    "support": "below",
    "ground": ["farmland"],
    "crop": { "averageSeconds": 900, "plantChance": 0.9 },
    "drops": [{ "item": "ruby_seeds", "when": { "age": ["0", "1", "2", "3", "4", "5", "6"] } },
              { "item": "ruby", "when": { "age": "7" } }],
    "textures": { "all": "ruby_ripe", "slots": { "young": "ruby_young", "growing": "ruby_young", "ripe": "ruby_ripe" } }
}
```

Texture pattern for fire: `fire` (flames on a transparent background, primary at their edges, secondary in their core).

Texture patterns for farms: `crop`, `root_crop`, `stem`, `farmland`, `pumpkin_side`, `pumpkin_top`, `melon_side`, `hoe`, `wheat`, `bread`, `carrot`, `melon_slice`, `pie`.

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
| `smelting`: `input`, `result` (`item`, `count` 1–64), `seconds` (0.05–3600, default 10), `experience` (0–100 per item, default 0, server side only, E-398), `category` | Per category and input the first recipe in load order counts; a later one is reported as a warning. To change a base recipe, reuse its name. |
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

Light and shapes (decision E-104): `lightOpacity` counts for states that are a full cube. A state of a light-dimming block (`lightOpacity` above 0) whose shape is not a full cube (stairs, bottom and top slabs, walls, carpets) lets light into its cell and only stops it at the sides its boxes cover completely: a bottom slab is lit from above and the sides but shades the cell below, a top slab keeps the sky out of its cell, a stair passes light through its open quarter. Blocks with `lightOpacity` 0 let light through whatever their shape. A value above 0 has no effect on a shape that never covers a whole side (crossed planes, torches, fences): give such blocks `lightOpacity` 0. The handbook shows the effective value: 0 for such shapes, "only at the sides its shape covers fully" for slabs and stairs.

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

The part `<material namespace>:<material>_<suffix>` copies textures, tool, tool level, sound material, flammability and fuel (× `fuel`) and hardness (× `hardness`) from the base block and drops itself; `@` in the recipe stands for the base block; `%1` in a translation is the material's name (translation key `material.<namespace>.<name>`, override with `translation`). Base families: `stairs`, `slab` (wood and stone), `wall` (stone), `fence`, `fence_gate` (wood). Entries of `exclude` are family names; a material can leave out families of other packs by their full name (`qtcraft:wall`). Base families also include `carpet` (kind `wool`). Base materials: `oak`, `birch`, `spruce` (planks), `cobblestone`, `stone` (no wall), `brick`, `sandstone`, `stone_bricks`, `polished_stone` (no wall), `smooth_sandstone`, `mud_bricks`, and the sixteen wool colours (`white_wool` … `pink_wool`, carpets only). Nothing is made for excluded materials: blocks listed by an exclusion entry (base list in `materials/excluded.json`: ores, sand, gravel, clay, dirt, grass, snow, ice, logs, leaves, saplings, cactus, glass, water, bedrock, crafting table, furnace, chest) and every block that is no plain building block (not an opaque solid full cube without states, a machine, crafting table or chest, unbreakable or glowing), the latter with a warning. A block defined explicitly under a part's name is never replaced, nor is a recipe a pack defines under that name. Recipes take named ingredients before tags, so a tag never uses up an item a named ingredient needs.

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
| `flow.dropSearch` | 0–8 blocks (default 4, lava 2) searched sideways for the nearest drop: a spreading liquid only runs towards the nearest drops it can still reach, like the vanilla game; 0 or no drop that near spreads it evenly. Server side only. |
| `flow.infinite` | Two sources next to each other over solid ground make a new source (water). |
| `flow.movement` | 0.05–2: factor on swimming speed inside the liquid (lava 0.45). |
| `flow.bucket` | Item an item with `scoops` (the empty bucket) becomes when it takes a source; without it the liquid cannot be taken. |
| `flow.bottle` | Item an item with `"bottles": true` (the glass bottle) becomes when used on the liquid; the liquid stays (water: water bottle). |
| `flow.reactions` | Touching the liquid `with`, a source of this liquid turns into `source`, a flowing part into `flowing` (lava next to water: obsidian or cobblestone). |
| `flow.sound` | Played now and then where the liquid spreads. |
| `contactDamage` (any block) | Damage per hit to players and creatures touching the block or standing on it (magma), every half second at most; items in it burn. |
| `slowsInside` (any block) | 0.01–1: factor on the movement of bodies inside a block that is neither solid nor liquid; falls through it stay slow and do no damage (cobweb 0.25). |
| `surface` (any block) | `{"speed": …, "jump": …}`, each 0.1–2: factors on walking speed and jump strength of bodies standing on the block (mud 0.6 and 0.93). |
| `hardens` (any block) | `{"with": <block>, "into": <block>}`: touching `with` (any state) on any side, or landing in it while falling, the block turns into `into` (concrete powder in water). |
| `fire` (any block) | `true`: the block is fire (decision E-392). It must be neither solid nor liquid, have `lightOpacity` 0 and be neither flammable nor eternal. It stands on a solid top face or next to a flammable block, ages one step every 30 to 39 ticks and goes out after 45 steps (after 9 without a flammable block next to it; a new fire starts with the age of the fire that lit it); while fire spreads, each step burns flammable neighbours away (they become this fire or air, without drops) and lights the air around it next to flammable blocks. Spreading fire places the block of the fire that spreads, so a mod can add a fire of its own. The base game's `fire` has light 15, `contactDamage` 1, no item and the `cross` shape. |
| `flammable` (any block) | `{"ignite": 0–100, "burn": 0–100}`: `ignite` is how readily fire near the block catches on in the air beside it (leaves, wool 30, grass and flowers 60, wood 5), `burn` how readily fire next to it burns it away, in 300 per step (leaves, wool 60, planks 20, logs 5). Building parts take the values of their material. Liquids cannot be flammable. |
| `eternalFire` (any block) | Fire standing on the block never ages and never spreads (the base game's magma block); it does not count toward the fire caps `firesPerChunk` and `fires` (decision E-394). When the block goes away, the fire needs room in the caps again or goes out. |
| `setsAlight` (blocks that are not solid) | Seconds (0–60) a player or creature touching the block burns for (decision E-393; fire 8, lava 15): one health point a second, through armour, until the time is over or water puts it out. The block is looked for like one with `contactDamage`. |
| `sparks` (any block) | Chance in 100 (lava 3) that a block update of the block lights fire in one air cell right next to it that touches a flammable block, as a new spark (decision E-393). Only updates count (something next to it changed, it flowed), never random ticks, so a still lake of it lights nothing; nothing at all while fire does not spread (game rule `fireSpread` off or `fireSpreadRadius` 0). |
| `washable` (any block) | A liquid flowing in breaks the block with its drops; default for `cross` shapes, set on the base torch (ladders and the mine lamp hold the liquid back). The cobweb keeps it (flowing water clears webs), but sets `"support": "none"` instead of the `below` of `cross` shapes, because it hangs in cave air. |

Food with `emptiesTo` leaves that item in the hand when eaten (mushroom stew: the bowl). A bucket is an item with `"places": "<liquid>"` and `"emptiesTo": "qtcraft:bucket"`; using it pours a source and leaves the empty item in the hand. Burnt as fuel (`fuelSeconds`, the lava bucket) it leaves the empty item in the fuel slot, where it may stay or be taken out (nothing else can be put next to it); fuel with `emptiesTo` must have `"maxStack": 1`, otherwise the item is rejected. `"scoops": true` makes an item (the empty bucket) aim at liquids and take sources; `"bottles": true` (the glass bottle) aims at liquids too but fills from them (`flow.bottle`) and leaves the source. An item with both is rejected.

Blocks with `"falls": true` (solid blocks only) fall as entities while the cell below is air, liquid or replaceable, and become blocks again where they land, or drop as items when that cell is taken (a torch) or the block would end up inside a player or creature. Plants use `"support": "below"` (default for `cross`) with `"ground": [block names]` (instead of any full solid top face) and `"groundLiquid": "qtcraft:water"` (the liquid must touch the ground block from a side, unless the ground is the plant itself, as for sugar cane). World generation places single blocks with features of `"type": "plant"` (`"block"`, `"ground"`) and, with `"nearWater": true`, only on ground at sea level with water next to it (sugar cane uses a `column` feature with it).

Features of `"type": "pool"` sink a basin of a liquid (`"block"`, e.g. `qtcraft:lava`) into cave floors. They are not listed by biomes (a biome naming one gets a warning) but tried in every chunk: `attempts` (1–8) tries, each taken with `chance` (0–1), look for a cave floor in a random column at or below a random height between `minY` and `maxY`; the basin reaches up to `radius` (1–6) blocks from that column and `depth` (1–4) blocks down, narrowing with depth. Cells on the chunk's edge and every cell whose sides or bottom would not hold liquid (air, liquids, non-solid or replaceable blocks) are left out, and a pool not open to the cave above is not placed; cells under another liquid are left out too, and the rock hollowed over a pool keeps any block that holds back another liquid or carries a falling block, so generated pools never flow or meet other liquids. With `"surface": true` the basin is dug into dry ground under open sky (after trees and plants; plants on the cells go), and `"biomes"` limits a pool to some biomes. The base game places `qtcraft:lava_pool` (y 12–44, above the lava lakes that fill the caves up to y 10; two attempts at 0.3, radius 3, depth 2) and the surface lake `qtcraft:lava_lake` (deserts, mountains, plains and taiga, one attempt at 0.03, radius 5, depth 3); the example mod adds `rubies:molten_ruby_pool`. All mods together may define at most 64 pool attempts per chunk.

Creatures may fly (`"flying": true`: no gravity, 1.2–3 m above the ground), glow (`"glow": 0–15`: drawn that bright, pulsing; it lights nothing around them), belong to the ambient category (`"spawn": {"ambient": true}`, own cap of 10 per player, gone at daybreak when they spawn at night), be limited per kind (`"spawn": {"maxCount": 8}`), spawn only in some biomes (`"spawn": {"biomes": ["qtcraft:swamp"]}`), only in a share of the chunks chosen by the world's seed (`"spawn": {"chunkChance": 0.1}`, like the base game's slime chunks) or only up to a height (`"spawn": {"maxY": 40}`), and spawn at further places and times through extra rules (`"spawn": {"also": [{"time": "night", "biomes": ["qtcraft:swamp"], "maxY": 70}]}`, up to 8, each with its own `time`, `blocks`, `weight`, `caves`, `maxLight`, `chunkChance`, `maxY` and `biomes`) and be caught: `"catch": {"with": "qtcraft:glass_bottle", "item": "qtcraft:firefly_bottle"}` turns one held `with` item into `item` when the player uses it on the creature. No creature spawns on a block with `contactDamage` or with its feet or head in a block with `slowsInside`, even if its spawn `blocks` lists that block, so mod blocks like magma or cobwebs keep spawns away by their fields.

**AI and abilities** (decision E-423, server side only): `"ai"` chooses what decides what the creature does: `passive` (wanders, flees when hurt; the default), `hostile` (chases and strikes players; needs `"hostile": true`, the default for hostile creatures) or `villager` (lives by its villager settings; goes with `"villager"` and is its default). `"abilities"` says how it finds its way: `opensDoors` (opens closed wooden doors and gates on its path; only the villager AI does so far), `climbs` (`none`, `ladders`: holds on to ladders and goes up and down along them; `walls`: for climbers like spiders, on paths like `ladders` until they come), `swims` (moves through water as freely as on land; a walker uses water only without a dry way) and `divesIntoWater` (drops deeper than 3 blocks, down to 12, into water at least two blocks deep; default `true`). The villager AI starts from `{"opensDoors": true, "climbs": "ladders", "swims": false, "divesIntoWater": false}` (villagers keep to their boardwalks), the others from `{"opensDoors": false, "climbs": "none", "swims": false, "divesIntoWater": true}`; the fields given replace those values. The base game's `villager` names its abilities in its file.

**Archers** (decision E-426, server side only): `"ai": "archer"` (needs `"hostile": true`) goes with an `"archer"` object, and that object only with it: `projectile` (the projectile kind it shoots, required; a name no pack has makes it a melee `hostile` creature with a warning), `drawSeconds` (0.1–10, default 1: how long it draws before a shot), `cooldownSeconds` (0–60, default 1: how long it waits after a shot before it draws again), `speed` (1–100 m/s, default 25; the arrow's damage grows with it: 2 per 20 m/s, rounded up, so 3 at 25 m/s), `spreadDegrees` (0–45, default 1.5: random deviation of each shot), `range` (2–64 m, default 15: it shoots at targets it sees up to this far, measured horizontally like `minDistance` and `keepDistance`), `minDistance` (default 5: it backs away from a closer target) and `keepDistance` (default 12: it walks towards a farther target, or one it does not see, and circles sideways in between); `minDistance` < `keepDistance` ≤ `range`. It aims at the middle of the target with the lead of its movement and the drop and drag of the projectile, and strikes a target in melee (`attackDamage`, `attackRange`, `attackCooldown`). Its arrows cannot be picked up. While it draws, the client shows the creature with the `Drawing` flag (arms raised and drawn, like a player). `"heldItem": "<item>"` puts an item into the right hand of a creature with a skeleton model (the skeleton's bow); an item no pack has is not shown. `"burnsInDaylight": true` sets a creature alight in direct sunlight for 8 s, as fire does: by day, with full sky light at its head (no roof, leaves or other shade above), neither in water nor in the rain. Creatures wear no armour, so nothing else protects them. The showcase gives such creatures a pen with an opaque roof. Creature drops (`drops`) take `item`, `count` (1–64) and `chance` (0–1, default 1) per entry.

Further procedural texture patterns: `lava`, `flower`, `tall_grass`, `mushroom`, `sugar_cane`, `bottle` and `bucket` (contents in the secondary colour, transparent `#00000000` for empty), `lantern` (a lamp housing with a glowing window, for `uv: "box"`). Since the block and recipe expansion: `nugget`, `metal_block` (storage blocks), `bars`, `bookshelf`, `paper`, `book`, `bowl`, `stew` (a bowl with its contents in the secondary colour), `apple`, `leather`, `scrap`, `string`, `cobweb`, `dead_bush`, `fern`, `firefly_glass`, `barrel_side`, `barrel_top` and `stained_glass` (tinted glass that stays partly transparent). Since the capes: `emblem`, pixel art from the data (decision E-241): `"bitmap"` holds up to 16 rows of up to 16 characters (top row first), `"palette"` maps single characters to `"#rrggbb"` (up to 16 colours); every other character and every pixel the bitmap leaves out shows finely woven cloth in the `primary` colour. `content/qtcraft/textures/cape_green_emblem.json` is an example.

Characters come from data as well: `models/*.json` defines skeletons (bones with a role, a pivot and boxes with slot names), `appearance/*.json` outfits (a texture and an optional tint per slot) and hair styles (extra boxes on the head). A creature can use `"skeleton": "qtcraft:humanoid", "outfit": "mymod:robe"` instead of `model` boxes and is then animated like players (walking, attacking, hit, death; `"armsForward": true` for zombie-like arms). New outfits and hair styles appear in the character screen of players who have the mod; `"playable": false` keeps a creature outfit out of that list, and the server refuses it for players. Tinted textures should be light (near white); the player's colour is multiplied in.

Capes are appearances of `"kind": "cape"` (decision E-240) and appear in the character screen's cape list. The cape hangs from the top of the torso's back and swings with the movement (lifted by walking and more by sprinting, upwards when falling, flat when sneaking, swaying when standing). `"shape"` sets `width` (0.05–1 m, default 0.6), `length` (0.05–1.6 m, default 0.95), `thickness` (default 0.04 m) and `offset` (gap to the back, 0–0.3 m, default 0.02). Layers: `outside` (required) textures the outer side below the emblem, `emblem` the upper square part of the outer side (as long as the cape is wide; the whole outer side uses `outside` without it), `inside` the side towards the back and `edge` the narrow sides; missing ones fall back to `outside` (emblem, inside) and `inside` (edge), tints work as for outfits. A skeleton creature can wear one with `"cape": "mymod:red_cape"`. Example:

```json
{
  "name": "red_cape",
  "kind": "cape",
  "shape": { "width": 0.6, "length": 1.1 },
  "layers": {
    "emblem": { "texture": "red_cape_emblem" },
    "outside": { "texture": "cloth", "tint": "shirt" },
    "inside": { "texture": "red_cape_inside" }
  }
}
```

### Signal Dust components (stage 29a)

A block joins circuits with a `signal` object (decision E-333). `type` picks a behaviour the server implements; the other fields tune it, so a mod adds its own sources (levers, buttons, plates, torches, repeaters), wires and consumers ("slots": lamps, openers, pistons) in data. A block without `type` can still set how pistons treat it (`movable`) and whether it carries power (`conducts`).

```json
{ "name": "ruby_lamp", "sound": "glass", "hardness": 0.5, "textures": { "all": "ruby_lamp" },
  "signal": { "type": "lamp", "active": false, "counterpart": "ruby_lamp_lit", "offDelayTicks": 4 } }
```

| Field | Meaning |
|-------|---------|
| `type` | `dust`, `lever`, `button`, `plate`, `torch`, `repeater`, `lamp`, `opener`, `piston`, `piston_head`; since stage 29.2 also `comparator`, `observer`, `hopper`, `dispenser` (below); since stage 32.2 `rail` (powered and activator rails, with the state `powered`; see the rails below) |
| `power` | Strength a source emits, 1–15 (default 15) |
| `pulseTicks` | Buttons: ticks until they spring back (stone 20, oak 30) |
| `offDelayTicks` | Lamps: ticks until they go dark after losing power (default 4, 0 at once) |
| `detects` | Plates: `all` (any entity, default), `living` (players and creatures) or `carts` (minecarts only: a detector rail) |
| `reach` | Rail slots: how many rails of the same block along the line pass the power on (default 8, 0–64) |
| `sticky`, `pushLimit`, `head` | Pistons: pull back the block in front, blocks pushed at most (default 12), the block of the extended head |
| `quasiConnectivity` | Pistons: also powered through the block above them, noticed only on a block update next to them (default `true`; the world's game rule `quasiConnectivity` can switch it off for all) |
| `active`, `counterpart` | Torches and lamps come as two blocks (light belongs to a block): `active` marks the lit one, `counterpart` names the other one with the same properties |
| `movable` | How pistons treat the block: `default` (unbreakable blocks stay, blocks that hang, wash away, flow or give way break, the rest moves), `push`, `immovable`, `breaks` |
| `conducts` | Whether a full solid opaque block passes power (default `true`; pistons and heads `false`) |

A piston's `head` must be a `piston_head` block with the piston's `facing` values and `sticky`; a torch's or lamp's `counterpart` must be the same kind with the opposite `active` and the same states. A block whose link is broken takes no part in circuits (a warning when the content loads). Internal variants like a lit lamp set the block field `"item": false`: they get no item of their own.

Each kind needs certain states, which the parser checks: dust `power` (`"0"`–`"15"`) and `north`, `east`, `south`, `west` (`none`, `side`, `up`); levers, buttons and plates `powered`; torches `attach`; repeaters `facing` (four sides), `delay` (`"1"`–`"4"`), `powered`, `locked`; openers `open`; pistons `facing` (six sides, a `rotate` of the shape turns up and down too) and `extended`; heads `facing` and `sticky`. Chests, barrels and furnaces move with their contents; mods' storage blocks and machines do too.

### Signal machines and comparator readings (stage 29.2)

Four more `type`s (decisions E-342 to E-346): `comparator`, `observer`, `hopper`, `dispenser`. A mod's block of such a kind behaves like the base block of that kind; its states are checked like the others: comparators `facing` (four sides), `mode` (`compare`, `subtract`), `powered`, `power` (`"0"`–`"15"`); observers `facing` (six sides, the direction it watches) and `powered`; hoppers `facing` (`down` and four sides) and `enabled`, plus a `container`; dispensers `facing` (six sides) and `triggered`, plus a `container`, and `"uses": false` for a dropper (only throws). Dispensers take `quasiConnectivity` like pistons. A block's `container` may set `"columns"` (slots per row of its screen, 1–9, default 9), and `"facing": "into"` makes a block face into the clicked block (down when placed on top).

```json
{ "name": "ruby_hopper", "shape": "qtcraft:signal_hopper", "facing": "into", "render": "cutout", "hardness": 3,
  "container": { "slots": 5, "columns": 5 },
  "states": { "facing": ["down", "north", "east", "south", "west"], "enabled": [true, false] },
  "textures": { "all": "ruby_block", "slots": { "inside": "ruby_block" } },
  "signal": { "type": "hopper", "comparator": "contents" } }
```

**What a comparator reads** is set per block with `signal.comparator`, also on blocks that are no component (then `signal` holds only this field):

| Value | Reading |
|-------|---------|
| `"contents"` | Fullness of the block's container or machine: 0 when empty, else ⌊1 + 14 · mean of count/maxStack over the slots⌋ (needs `container` or `machine`) |
| `{"property": "<name>"}` | The index of the value of that state property, scaled to 0–15 (the property needs at least two values; the base game uses it for crop stages) |

**What a dispenser does with an item** is set per item with `dispense`: `throw` (default; also seeds, carrots and potatoes — they are thrown and plant themselves on farmland, never planted by the dispenser, E-294, E-345), `place_liquid` (pours the liquid block of `places` and becomes `emptiesTo`; needs both, and only liquids are poured), `scoop` (needs `scoops`: takes a liquid source and becomes that liquid's bucket), `equip` (needs `armour`: puts it on a player or armour stand in front), `ignite` (needs `ignites`: lights fire in front and wears the item by one, decision E-393). A failed action throws the item out, also in front of a container (as in the reference); a failed `ignite` keeps the item in the dispenser. A dropper ignores `dispense`; facing a container, it puts the item into it.

Hoppers reach a mod's containers and machines like the base ones: machines take items from above into their input and from the sides and below into their fuel slot (fuel only) and give their output downwards. A hopper collects items lying on any block above it whose collision does not fill its cell; a mod's slab or path block keeps working as a hidden collector.

### Armour and armour stands (stage 23)

An item becomes armour with an `armour` object (decisions E-234, E-235): `slot` (`head`, `chest`, `legs`, `feet`), `points` (0–20; 4 % less damage per point, at most 20 count), `set` (name of the set it belongs to; the showcase puts every set on a stand and the handbook groups by it; its title is the translation `armour_set.<namespace>.<name>`; default: the item itself) and how it looks: `layers` (at most 8) – `covers` (slots of the boxes of the humanoid model, e.g. `["torso", "sleeve"]`), `texture`, optional `front` (the face looking forward, e.g. a helmet with a transparent opening), `inflate` (metres added on every side, 0–0.2, default 0.03), `fromY`/`toY` (cut, model metres) – and `parts` (at most 16 extra boxes): `bone` (`head`, `body`, `right_arm`, `left_arm`, `right_leg`, `left_leg`), `from`, `to` (model metres like the model's boxes), `texture`, optional `front`. `durability` counts hits. Armour never stacks. Layers and parts turn with the body parts in every animation. Recipes are ordinary crafting recipes. New texture patterns for icons and armour faces: `helmet`, `chestplate`, `leggings`, `boots`, `armour_plate`, `armour_leather`, `helmet_front`, `visor`, `armour_trim`, `armor_stand`.

`"placesStand": true` makes an item place an armour stand (E-238), which drops that item again; `"placesCart": true` makes it place a minecart on a rail (E-382). A block with `"rail": true` is a rail: it needs the states `facing` (`north`, `east`, `south`, `west`) and `shape` (`straight`, `ascending`, `curve`), cannot be solid, and takes its state from the rails around it; draw it with a shape turned by `facing` (the base shape `qtcraft:rail`). A rail without the `shape` value `curve` never bends (powered, detector and activator rails, E-384); `"railAction": "boost"` (powered rail: speeds carts up while its state `powered` is true, brakes them while it is false) or `"activate"` (activator rail: while powered the rider gets out and hopper carts stop pulling, unpowered they pull again) needs the state `powered`, cannot have the `shape` value `curve` and usually goes with `"signal": {"type": "rail", "reach": 8}`, a slot powered while it or a rail of the same block within `reach` along its line gets power. A detector rail is a plate that only carts press, `"signal": {"type": "plate", "detects": "carts", "comparator": "carts"}` (a comparator behind it reads the container cart on it, E-385). An item that places a cart can carry a block, `"cartBlock": "chest"` (a container cart with that block's slots) or a Signal Hopper (a hopper cart, E-386); `"supportsAbove": true` lets a block without a full top carry rails and other blocks that need support (the Signal Hopper). Structures may put minecarts on their rails: `"minecarts": [[x, y, z], {"at": [x, y, z], "item": "chest_minecart"}]` (the rail's cell); `contents` at the cell of a container cart fill the cart. Structures may bring stands: `"stands": [{"at": [x, y, z], "yaw": 180, "properties": ["equip qtcraft:iron", "pose right_arm -90 0 0", "name Knight", "showname true"]}]` (properties as for `/armorstand`, E-239; ones that do not apply are logged and skipped).

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

### Handbook (stage 15)

The handbook (F1 in the game, `qtcraft-headless --export-handbook <dir>` as Markdown) is built from the loaded content, so every block, item, recipe, smelting recipe, creature, biome, liquid, ore, feature and item tag of a mod appears automatically, in its own section after the base game (grouped by the namespace of its names). What a mod can add:

- **Texts:** blocks, items, mobs and biomes take an optional `"description"` (a translation key; default `<translation>.description`, e.g. `block.mymod.gem_ore.description`). Fill it in the `lang/` files; without it the page shows only the facts. Generated building parts share `part.<namespace>.<family>.description` of their family. Titles of features come from `feature.<namespace>.<name>` (else the file name), of item tags from `tag.<namespace>.<tag>`, of part families from `handbook.family.<family>`. Texts may use a small Markdown subset: paragraphs, `- ` lists, `**bold**`, `` `code` ``. Descriptions and chapters may quote values as `{name}` instead of writing numbers: rule values of the game (below) and content values `<block>.toolLevel`, `.slots`, `.flowDistance`, `.light`, `.hardness`, `<item>.toolLevel`, `.food`, `.fuelSeconds`, `.durability`, `<mob>.health` (e.g. `{mymod:gem_ore.toolLevel}`).
- **Chapters:** `handbook/<name>.json` with `name`, `order` (sort number, base chapters use 10–120), `icon` (an item), `sections` (1–32 lower-case words) and `related` (content names linked at the end). The title is the translation of `chapter.<namespace>.<name>`, each section `chapter.<namespace>.<name>.<section>.title` and `.text`. A text may quote rule values of the game as `{name}` (e.g. `{hunger.maxFood}`, `{day.length}`; the list is `QcHandbookRules::values()`); an unknown name is reported as a handbook problem.

```json
{ "name": "gems", "order": 200, "icon": "gem", "sections": ["finding"], "related": ["gem_ore"] }
```

`qtcraft-headless --mods-dir <mods> --export-handbook <dir>` (without `--mods-dir` the export covers only the base game) prints every problem (links to unknown content, unknown rule values) and writes the pages to check a mod.

### Structures and the showcase world

A structure (`structures/<name>.json`, decision E-219) is a building made of block layers. `palette` maps single characters to block states (`"T": "torch[attach=north]"`, at most 64; a space in a layer keeps whatever is there, so map air explicitly to clear cells). `layers` go from the bottom up; each layer is a list of rows from north to south, each row a string from west to east (at most 64 × 64 × 64). Optional: `mobs` (`{"mob", "at": [x, y, z]}`), `stands` (armour stands, see above), `contents` (`{"at": [x, y, z], "items": [{"item", "count"}]}`, fills the chest or machine in that cell; machines take input, fuel, output), `labels` (`{"at", "text" (a translation key), "detail", "heading"}`), `updates` (cells whose block update runs after placing: a liquid source starts to flow, sand falls) and `growing` (saplings that grow). Positions are local; points may lie up to 4 blocks outside the structure (labels above a roof). The translation key of its title is `structure.<namespace>.<name>`.

`"showcase": {"order": 100}` puts the structure into the showcase world as a demo (ordered by `order`, the base game uses 10–70). There, layer 0 is the ground layer (replace it for pools and floors). The showcase also picks up everything else of a mod by itself: every state of every block on a podium (sorted by the categories of `worldgen/showcase.json`), every creature in an enclosure, every item in a chest and every tree feature with its saplings. The example mod adds `ruby_shrine`:

```json
{
    "name": "ruby_shrine",
    "showcase": { "order": 100 },
    "palette": { "B": "ruby_block", "S": "qtcraft:stone_bricks", "L": "ruby_lantern", "R": "ruby_rose", "G": "qtcraft:grass_block" },
    "layers": [
        ["SSSSS", "SGGGS", "SGSGS", "SGGGS", "SSSSS"],
        ["L   L", " R R ", "  B  ", " R R ", "L   L"],
        ["     ", "     ", "  B  ", "     ", "     "]
    ],
    "labels": [{ "at": [2.5, 4.0, 2.5], "text": "block.rubies.ruby_block" }]
}
```

`worldgen/showcase.json`: `timeOfDay` (seconds after sunrise, 150 = noon), `layers` (`{"block", "depth"}` from the bottom up), `podium`, `enclosure`, `container` (blocks), `lookout` (a structure placed in front of every area), `rowWidth` (metres) and `categories` (`{"name", "translation", "match"}`; `match` may hold `liquid`, `part`, `light`, `plant`, `functional`, `falls` (true/false) and the lists `render`, `tool`, `nameContains`; a block goes to the first category whose conditions all hold, else to the last one).

### Villages (stage 37.2)

Villages (decision E-403) are built from structures and two kinds of data files. A **style** (`villages/<name>.json`) says how a village of some biomes looks:

```json
{
    "formatVersion": 1,
    "name": "desert",
    "biomes": ["desert"],
    "centre": "village_desert_well",
    "lamp": "village_desert_lamp",
    "path": [
        { "block": "terracotta", "step": "sandstone_slab" },
        { "block": "packed_mud", "step": "mud_bricks_slab" }
    ],
    "branchPath": { "block": "cut_sandstone", "step": "sandstone_slab" },
    "crops": [
        { "block": "wheat_crop", "weight": 2 },
        { "block": "potato_crop", "weight": 2 },
        "carrot_crop"
    ],
    "foundation": "sandstone",
    "pieces": [
        {
            "structures": [
                { "structure": "village_desert_house_small", "weight": 2 },
                { "structure": "village_desert_house_small_b" }
            ],
            "weight": 5
        },
        { "structure": "village_desert_temple", "weight": 2, "max": 1 },
        { "structure": "village_desert_pen", "max": 1, "chance": 0.4 },
        {
            "structure": "village_desert_fisher_hut", "max": 1, "water": 12,
            "jetty": { "deck": "birch_planks", "post": "birch_log", "width": 3, "over": 5 }
        },
        { "structure": "village_desert_tower", "max": 1, "minRoadLength": 200 }
    ],
    "layouts": [
        { "kind": "plaza", "weight": 3, "exits": [2, 3] },
        { "kind": "street", "weight": 2, "winding": 0.15 },
        { "kind": "tree", "arms": [2, 4], "bends": [1, 2] }
    ],
    "palette": { "oak_door": "spruce_door" }
}
```

`biomes`: the biomes built in this style; `fallback: true` makes it the style of every biome with villages that no style names (the base game's `plains`). `centre` (required): the meeting point in the middle, its centre on the village centre; the bell inside it is the meeting point. `lamp`: a lamp post set along the roads (optional). `path`: the surface of the roads from the centre (default gravel), `branchPath` of the roads branching off them (default `path`); each is one surface or a list of up to 8 (decision E-406), and a village takes one per rank, by `weight` (1–1000, default 1), so two villages of a style may have different roads. A surface is a block name or an object with `block` and optionally `weight` and `step` (a slab): the step is laid on a road cell next to a road cell one block higher, so a road over a terrace step reads as a stair (steps of two blocks stay as they are; a road ends at higher ones). Where a branch meets its road, the road keeps its surface. `crops` (up to 16, decision E-406): the crops of the fields, each a block name or `{ "block", "weight" }`; every crop cell of every piece of the style (a block with `crop`) is planted with one of them, one crop per row (a run of crop cells along x in one layer of the structure), each plant ripe with a chance of one half and else at an earlier stage. Without `crops` the pieces keep the crops they are written with. A mod adds a seed to the villages of a base style by extending the style (see "Extending a style" below), or adds a style of its own. A crop that is no block with `crop` is left out with a warning. `foundation`: fills the ground under a piece where it lies lower than the floor (default cobblestone). `pieces` (1–32): the kinds of buildings, each with a relative `weight` (1–1000) and an optional `max` per village (0–64, 0 = no limit), and either one `structure` or up to 16 blueprints in `structures` (`structure` with its own `weight` among the blueprints, default 1); every building of the kind is one of its blueprints, chosen by weight (decision E-404), so a mod adds a house variant by adding a blueprint. **Rules** make a kind conditional (E-404): `chance` (0–1, default 1: drawn once per village), `minRoadLength` (0–4096: only in villages whose roads are at least this many blocks long together, measured along their middle lines: the village's size, decision E-408; the base game's watchtower needs 200, about one village in ten) and `water` (1–32: only beside the end of a road with water at most this many blocks straight on, with a `jetty` from the road end over the ground to the water and `over` (1–16, default 4) blocks out over it, just above the water, `width` 1–5 (odd, default 1), `deck` and `post` blocks (default oak planks and log; posts under the edges reach down to the ground, and the style's palette applies)). Kinds with rules are placed first, each up to its `max` (at least one), on a random free spot along the roads; the weighted filling never takes them. `palette` (up to 64 entries): block name → block name, applied to every structure of the style (states keep their properties where the new block has them), so a mod makes a new style from existing houses (the base game's `taiga` is the plains houses in spruce; `snowy`, the tundra's style, adds its own igloo and snow-covered cabin to them). `layouts` (1–16, decision E-408): the street plans of the style's villages, one chosen per village by `weight` (1–1000); without the field a mix of all six kinds (street 3, cross 2, T 2, L 1, plaza 1, tree 2). `kind` (required) is one of `cross` (four roads from the centre, one to each side), `street` (one long main road through the centre, along an axis or a diagonal, with side roads), `t` (three roads: two opposite, one across), `l` (two roads at a right angle), `plaza` (a ring road around a square with the centre and a side road from the centre to the ring, roads out from the ring, each leaving its side of the ring off the middle by up to half the ring's distance so that they never line up into a cross; houses stand outside the ring, the square stays open; a ring that water or a steep step would cut makes the village a cross) or `tree` (`arms` roads from the centre at random angles, the first on a diagonal, each bending `bends` times by 45 degrees). Roads are three blocks wide and run in eight directions; diagonal roads are staircases of cells, and houses at them face the road along the nearer axis with a forecourt of side road three blocks wide from their door to the road. Parameters (each kind has its own defaults): `armLength` (a share of `radius`, 0.2–1, the length of a road from the centre or from the ring), `branches` (`chance` 0–1 per spot, `every` 0–64 blocks between spots along a road, 0 for one spot at 50–70 % of it; `length` a share of the radius), `arms` (2–5), `bends` (0–2), `ring` (8–20 blocks from the centre to the ring's middle line) and `exits` (1–4); a range is a number or `[low, high]`, drawn per village. `winding` (0–1): the chance per village that its roads wind with the terrain, turning by 45 degrees every 6 to 11 blocks towards the flatter neighbouring direction (at random on level ground), at most 45 degrees off their heading. `diagonals` (default true): false keeps every road on the axes and switches winding off. A style over water never lays diagonal or winding roads (a house's front lies along a boardwalk). A later pack replaces a style of the same name; a style without a known centre, foundation or at least one known surface per road rank is left out with a warning (unknown surfaces and steps are skipped with one), unknown pieces are skipped.

A **piece** is an ordinary structure (see above) built for a road on its south side: the door is in the last row of the first layers, layer 0 is the floor that replaces the ground's top block, and the planner turns the piece in quarter turns so the door faces the road (states turn along, so stairs keep their slope). Where the ground is lower than the floor, the foundation fills it (up to 16 blocks); above the footprint the terrain is cleared. Beds (blocks with `"bed": true`, their head), workstations and the bell inside a piece are listed in the village's state, and the creatures in its `mobs` (an animal pen's pair) appear, persistent like natural animals, when their chunk is first seen. The other extras of a structure (`contents`, `stands`, `labels`, `minecarts`, `updates`, `growing`) are left out in villages, with a warning when the style is loaded: chests and furnaces of a piece stand empty, saplings are only planted. Every outer door gets a doorstep at the floor's height in front of it (the ground there is levelled and laid as side road), so the door opens onto walkable ground.

Further style fields (decision E-421): `look` dresses the style's villagers (see Villagers below; empty lists take the species' default look), `villager` names the creature kind living in its villages (default: the settings' `villager`), `golem` changes the settings' golems for its villages (an object with fields of the settings' `golem`, each over the settings' value as all packs leave it, so `{ "minVillagers": 5 }` keeps the settings' creature kind; or `false` for none), and `residents` (up to 8) lists other creatures of its villages, each `{ "mob", "min", "max" }` (0–16, default one each), such as the cats of a later stage. Residents are data only for now: no village spawns them yet (the place to do so is beside the pen animals of the village plan, `QcVillagePlan::creatures`, or `QcMobManager::spawnVillagers`).

**Extending a style** (decision E-421): a style file with `"extends": "<style>"` starts from that style as the earlier packs and files left it. Without its own `name` it changes that style in place (a patch); with a `name` it is a new style made from a copy, which starts without the base's `biomes` and as no fallback (it builds nowhere until it names its own biomes or sets `fallback`), so a copy never takes over the base's villages. Fields it gives replace the base's (`ground`, `stilts` and `wetland` together; `golem` field by field over the base's), the others stay; its `remove` object takes `biomes` from the base's list, its `add` object appends to the base's lists: `biomes` (each biome once), `crops`, `pieces` and `layouts`, each within its limit. An extension may extend a style another extension made (a named one of an earlier file or pack). Two mods that each add a crop and a biome to the plains both keep theirs:

```json
{ "formatVersion": 1, "extends": "qtcraft:plains", "add": { "biomes": ["qtcraft:forest"], "crops": [ { "block": "qtcraft:pumpkin_stem", "weight": 2 } ] } }
```

Conflict rules: in each pack the files without `extends` load first (each replaces a style of the same name whole, with all extensions of earlier packs), then the extensions in file-name order; packs load in their order, so an extension applies to the style as it stands when its pack is loaded, and a later full definition of that style drops it. `add` or `remove` without `extends` and an extension of a style no earlier file has are left out with a warning (when a later file of the same pack defines that style, the warning says to rename the extension so it sorts after it). Unqualified names in an extension take its own pack's namespace, so a base style is extended as `qtcraft:plains` and the base game's biomes and blocks are written `qtcraft:forest`. **A biome belongs to one style:** when two styles name it, or two are the fallback, the style changed last keeps it (by pack order: the last pack that defined or extended it; within a pack the later file), with a warning; a mod that wants a base style's biome takes it with `remove` in an extension of that style. Unknown biomes, a `villager` that is no villager creature and unknown `residents` are warned about when the content is finished (an unknown golem creature is not: the rule rests until the content has it, see the settings below).

**Format versions** (decision E-421): style files, `worldgen/villages.json`, villager species, professions and trades carry `"formatVersion"` (a whole number, 1 when missing; the current format is 1). A file with a newer version than the game reads is left out with a warning naming both versions. Files of the earlier format are refused with where their content went: a pack's `villagers.json`, a style's `villagers` (now `look`), the settings' `workstations` and a profession's `workstation` (now the profession's `workstations`).

**Variation inside a blueprint** (decision E-404): where a village places a structure, two optional fields make each building different, drawn per building from the seed (the same world builds the same houses). `choices` is an object of named choices (up to 8), each a list of 1–16 options, one taken per building, all equally likely (list an option twice to make it likelier); an option is an object of name swaps like the style's palette, applied to the structure's block names (states kept) and to its creatures' names, `{}` keeps the structure as written. The style's palette is applied first, so a taiga style that turns oak into spruce keeps its spruce even where a choice would turn oak into birch. `parts` (up to 16) are optional pieces of the building, each with a `name`, a `chance` (0–1, default 0.5), its north west bottom corner `at` ([x, y, z] inside the structure) and `layers` like the structure's own, in its palette (a space keeps the structure's cell); the structure's size must already hold them (a chimney on the ridge, flowers in a front row of grass, a shed beside the house):

```json
"choices": {
    "wood": [ {}, { "oak_planks": "birch_planks", "oak_log": "birch_log" } ],
    "animal": [ {}, { "pig": "sheep" } ]
},
"parts": [
    { "name": "chimney", "chance": 0.5, "at": [5, 5, 2], "layers": [ ["C"], ["C"], ["C"] ] }
]
```

Elsewhere (the showcase world, other structures) a structure is placed as written; the showcase's village rows show every blueprint with all its parts.

**Villages over water** (decision E-407): `"ground": "water"` (default `"land"`) makes a style stand on stilts, like the base game's `swamp`:

```json
{
    "name": "swamp",
    "biomes": ["swamp"],
    "ground": "water",
    "stilts": { "post": "spruce_log", "stair": "spruce_stairs", "height": 4, "spacing": 3, "maxDepth": 8, "water": 0.06 },
    "wetland": { "depth": 1, "margin": 4, "fray": 3, "blend": 4, "bottom": "mud" },
    "path": "spruce_planks",
    "branchPath": "spruce_slab[type=top]",
    ...
}
```

Every floor (layer 0 of every piece) and every road lies at one height, `height` (2–8, default 3) blocks above the water surface (the block below the sea level). A place carries stilts where its ground lies at most at that height and at most `maxDepth` (0–16, default 5) blocks below the water surface. A centre needs at least 60 % of the ground around it carrying stilts and at least `water` (0–1, default 0.25) of it under water; of the places tried in a region, the wettest one that fits wins. Pieces stand on `post` blocks (default spruce log) every `spacing` (1–8, default 3) cells and at their far edges, down to the bottom, instead of a foundation; their floors join the road directly (no strip of ground between them), and a piece is only placed where its whole front lies along the road. Roads are **boardwalks**: the road surfaces (`path`, `branchPath`; here a surface may be a block state such as a top slab, as may a `step`) at the floor height with posts on a world grid every `spacing` cells; they end where the ground cannot carry them. Where a boardwalk ends on dry land, `stair` blocks (default spruce stairs, facing up) lead down to the ground, one block per cell. The optional `wetland` shapes the ground around the village: within `margin` (0–12, default 4) blocks of its pieces and boardwalks, plus up to `fray` (0–8, default 3) more where a noise bends the edge, the ground lies `depth` (1–4, default 1) blocks below the water surface with a `bottom` block (default mud), deeper water filled up to it; over the next `blend` (1–8, default 4) blocks it rises back to the terrain; the stairs and a strip beyond them stay as they are (the way to land). No trees or plants grow in that area. Villagers walk the boardwalks; creatures do not climb ladders on paths (decision E-402), so a style over water reaches land by stairs.

The **settings** (`worldgen/villages.json`; a later pack's file changes the fields it gives and keeps the others, `golem` field by field, so a mod setting `spacing` keeps the villagers and golems): `spacing` (128–4096, default 384: the edge of the square regions, at most one village each), `chance` (0–1, that a region tries to have one), `attempts` (1–32: places tried for the centre), `radius` (16 to spacing / 2 − 16: how far pieces and roads reach from the centre), `maxHeightSpread` (largest height difference of the ground around the centre), `maxPieceSlope` (under one piece), `lampSpacing` (blocks between lamp posts), `bell` (the meeting point block), `villager` (the creature kind living in villages whose style names none, the base game's `villager`) and `golem` (decision E-415; a style may have its own): `mob` is the creature kind villages spawn as their golem. A village with at least `minVillagers` (1–1024) villagers and fewer creatures of that kind in its area than one per `villagersPerGolem` (1–1024) villagers spawns one within `spawnRadius` (2–32) blocks of its bell, on dry ground that does not hurt where the creature's whole body fits (the ground near the bell's height before roofs), at most once per `cooldownSeconds`, only with the game rule `mobSpawning` and below the server limit `villageGolems` (counted per golem kind). While the content has no creature of that name (the base game names `iron_golem`, which comes with stage 52.6) the rule rests and the server logs it once; a mod with its own golem names it here or in its style. Which blocks are workstations comes from the professions (see Villagers). A biome gets villages with `"villages": true`.

A block with `"bed": true` is two blocks long like a door is two high: it needs the states `facing` (`north`, `east`, `south`, `west`) and `part` (`foot`, `head`), else the block is rejected; placed from the foot, the head goes one block further in `facing`; breaking either half takes the other one, and only the foot drops the item.

### Villagers (stage 37.2b)

Villagers (decision E-411) are creatures of a kind with `"villager": "<settings>"`, naming the villager settings they live by (a `skeleton` is required, they are peaceful walkers; they never spawn by themselves, give them `"spawn": {"weight": 0}`; a kind naming settings no pack has, or settings left out for a broken `unemployed`, is no villager, with a warning). When a chunk of a village is seen for the first time, a villager of the kind its style or `worldgen/villages.json` names moves into every free bed whose head lies in it, up to the server limits `villagersPerVillage` and `villagers`. It claims the bed and, when it finds a free one, a workstation, opens wooden doors (blocks with `toggles` and an `open` state; iron doors without `toggles` stay shut) and is drawn in the look the server gives it, not in its `outfit`. Children belong to the kind of their parents, and only villagers of one kind breed together.

The **villager settings** (`villagers/<name>.json`, decision E-421; a later pack replaces settings of the same name) are those of one species (settings whose `unemployed` is no profession without workstations are left out with a warning); every species shares all professions and their trade files, and a style's `look` goes before the species' default look; the base game has one, `villager`, and a mod adds a second species with its own creature kind, settings and style:

```json
{
    "formatVersion": 1,
    "name": "villager",
    "unemployed": "unemployed",
    "routine": [ { "from": 0.0, "do": "wander" }, { "from": 0.04, "do": "work" }, { "from": 0.53, "do": "sleep" } ],
    "fleeDistance": 10,
    "shelterFromRain": true,
    "trading": { "levels": [0, 10, 70, 150, 250], "offersPerLevel": 2, "restocksPerDay": 2, "restockGap": 0.1 },
    "breeding": { "food": 3, "growUpSeconds": 1200, "cooldownSeconds": 300, "partnerDistance": 5, "shareAbove": 6, "shareDistance": 4, "youngSpeed": 1.3 },
    "look": { "outfit": "villager_tunic", "hair": ["short_hair", ""], "skin": ["#d8a07a"], "hairColor": ["#4a3020"],
              "shirt": ["#3a7bc8"], "pants": ["#34406a"] }
}
```

A **profession** (`professions/<name>.json`, decision E-421; a later pack replaces one of the same name, so two mods each adding a profession keep both) is shared by every species:

```json
{
    "formatVersion": 1,
    "name": "farmer",
    "workstations": ["composter"],
    "hat": "villager_straw_hat",
    "tendsCrops": true,
    "layers": { "torso": { "texture": "villager_farmer_apron", "tint": "none" } }
}
```

- Professions: `name`; `workstations` (up to 16 blocks the profession works at; the village state lists them, and the workstation kind of such a block is the profession's qualified name, e.g. `qtcraft:farmer`; none for the unemployed; a block two professions name goes to the later one, with a warning); `layers` (slots of the outfit replaced for this profession, written like an outfit's layers: an apron on the `torso`), `hat` (a hair-kind appearance worn instead of the style's hair) and `tendsCrops` (harvests the ripe crops within 8 blocks of its workstation inside the village and plants them again in one step; the harvest counts as the villager's produce, no items drop); optionally its own `routine` (as below, in place of its species') and `offersPerLevel` (1–8, in place of its species' `trading` value). A species' `unemployed` names the profession without workstations; an unemployed villager takes the profession of the free workstation it claims, and one that loses its workstation before it has any experience is unemployed again. Its offers are the trades files naming it (see Trading).
- `routine` (1–24 steps): from `from` (a share of the day: 0 sunrise, 0.25 noon, 0.5 sunset, 0.75 midnight) on the villagers do `do` until the next step, the last step lasting into the first of the next day: `wander` (stroll near home), `work` (at the workstation), `meet` (at the village's bell), `home` (by the bed) or `sleep` (in the bed; a villager gets up when it is hurt, its bed is gone or the routine says so).
- `fleeDistance` (0–32): villagers run home from hostile creatures this close; `shelterFromRain`: they go home while it rains at the village's middle.
- `breeding` (decision E-415): two grown-up villagers of a village, each holding `food` (0–64) produce, that see each other within `partnerDistance` (1–16) blocks by day (the routine says neither `home` nor `sleep`) and in dry weather at the village's middle get a child when the village has a free bed; each gives `food` and rests for `cooldownSeconds` (0–86400). The child moves into the free bed nearest to them, has the unemployed profession, wanders instead of working at `youngSpeed` (0.5–3) times the walking speed, is drawn at half size and takes a workstation once grown up after `growUpSeconds` (1–86400). Produce is what farmers (`tendsCrops`) harvest; a farmer holding more than `shareAbove` (0–1024) gives villagers within `shareDistance` (0–16) that hold less than `food` what they lack. Server limits: `villagersPerVillage`, `villagers`.
- `look`: the default look (an `outfit`, hair styles to choose from, an empty name for none, and lists of `skin`, `hairColor`, `shirt` and `pants` colours as `"#rrggbb"`, at most 16 each). A village style dresses its villagers in its own look with `"look": { ... }` in the same form; empty lists take the default look's. Each villager draws one of every list from its own seed. For every look and every profession with `layers` the server registers the joined outfit `<outfit>@<profession>` (not playable), which the clients receive with the other appearances.

The base game has farmers (composter, apron and straw hat), smiths (furnace, leather apron) and fishers (barrel, oilskin vest and hat) and the looks of the plains (tunic), desert (robe and headwrap), taiga (fur coat and hat), snowy (pale fur coat and fur hat) and swamp (smock, waders and reed hat). Trading (stage 37.2c, below) and breeding (decision E-415, above) build on a villager's profession, level, experience and produce, which are saved with it.

### Trading (stage 37.2c)

A villager with offers opens its trading screen when a player uses it (decision E-414). Its offers come from the files in `trades/`; how villagers level and restock from `trading` in the settings of their species (`villagers/<name>.json`):

```json
"trading": { "levels": [0, 10, 70, 150, 250], "offersPerLevel": 2, "restocksPerDay": 2, "restockGap": 0.1 }
```

- `levels`: the experience a villager needs for each of the five levels (novice, apprentice, journeyman, expert, master), starting at 0 and rising.
- `offersPerLevel` (1–8; a profession's own value takes its place): for each level it reached, a villager draws this many offers of that level from its profession's offers (by a stable hash of its look, the level and the offer's id, so it keeps them for good); a level with fewer offers gives all of them. Offers a villager already has stay when a mod adds offers to the level; only an offer that no longer exists is replaced by a new draw.
- `restocksPerDay` (0–8) and `restockGap` (share of a day): during work time at its workstation a villager whose offers were used restocks them, at most this often a day and this far apart.

A trades file (`trades/<name>.json`) lists offers of one profession:

```json
{
    "formatVersion": 1,
    "name": "farmer",
    "profession": "farmer",
    "offers": [
        { "level": 1, "costs": [ { "item": "wheat", "count": 20 } ], "result": { "item": "emerald", "count": 1 },
          "maxUses": 16, "villagerExperience": 2, "playerExperience": 1, "demand": 2 },
        { "level": 4, "costs": [ { "item": "emerald", "count": 3 }, { "item": "wheat_seeds", "count": 8 } ],
          "result": { "item": "melon_seeds", "count": 4 } }
    ]
}
```

- `level` (1–5, default 1): from which level a villager may offer it. `costs` (1 or 2 stacks) are what the player pays, `result` what they get; `count` 1–64 (at most the item's stack size). Items are checked when the server starts: an offer naming an unknown item is left out with a warning.
- `maxUses` (1–999, default 12): trades until the next restock. `villagerExperience` (default 2) and `playerExperience` (default 1, dropped as orbs) per trade. `demand` (0–64, default 0): an offer sold out at a restock asks for this many more of its first cost for each such restock, at most twice the base count; each restock where it was not sold out takes one step back.
- `id` (default `<first cost>><result>`, e.g. `qtcraft:wheat>qtcraft:emerald`): unique within the profession; the saved uses and demand of a villager's offers belong to it. A later file with an offer of the same id replaces that offer.
- A file of the same `name` as an earlier pack's replaces it; a file of another name adds its offers to the profession. The base game's files are `farmer`, `smith` and `fisher`; the unemployed have none.

The base game's currency is the emerald (ore deep in the stone in small veins, item, block). `/profession <profession> [level]` (operators) gives the nearest villager a profession and level, e.g. to try a mod's offers on a summoned villager.

### Commands (stage 24)

Mods cannot run code, so they add commands as **macros** (decision E-248): `commands/kit.json`

```json
{
    "name": "kit",
    "permission": "player",
    "usage": "/kit <item> <count>",
    "description": "Gives a starter kit.",
    "run": ["/give $1 $2", "/give torch 8"]
}
```

- `name`: the command players type (`/kit`), lower-case letters, digits and `_`, at most 32. It is not namespaced; a later pack replaces an earlier macro of the same name, and a command of the game always wins over a macro (the server ignores the macro then).
- `permission`: `player` (everyone may call it) or `operator` (default).
- `run`: 1 to 16 command lines starting with `/`, at most 256 characters each. `$1` … `$9` are the words after the macro's name, `$*` all of them; called with fewer words than the highest `$n`, the macro answers with its `usage`.
- Every line runs as the player (or console) who called the macro and is checked for him again: in the example a player without operator rights gets "Only operators can use /give". A macro never grants rights. Macros cannot call other macros.
- `usage` and `description` are shown by `/help` as they are (not translated).

Code that is compiled into the game (not mods) adds real commands through the registry: `QcServer::commands().add(QcCommandSpec{...})` with a name, aliases, the permission (`QcPermission::Player`, `Operator`, `Console`), whether it needs a player, the minimum number of words, usage and description (English texts marked with `QT_TRANSLATE_NOOP("QcServerMessages", …)`) and a handler that receives a `QcCommandContext` (source, words, whole line). Taken names are refused. See `src/server/commands/QcCommandRegistry.h` and the command groups next to it.

### Replacing existing content

A definition whose name already exists **replaces the earlier one completely** (decision E-026), for example `blocks/glass.json` with `"name": "qtcraft:glass"`. Copy the original file and change what you need; fields you leave out get their defaults, not the old values. Runtime ids stay the same, so saved worlds are not affected. Translations are merged key by key.

## Textures, texture packs and mods

A mod's `textures/` are content: the server sends them to every client, and they are the base level of the mod's namespace. Texture packs (client side, `docs/Texturpakete.md`, decisions E-297 and E-302) override them by name like the base game's (`textures/<mod id>/<name>.png|json` in a pack), and a pack with `"procedural": true` such as the built-in **QtCraft HD** paints a mod's procedural textures at its resolution too. A procedural definition may say how it looks above 16×: `"detail": {"grain": 0–1, "bevel": 0–1, "smooth": true|false}` (E-299). A mod can ship texture packs of its own in `texturepacks/<folder>/` (with `texturepack.json`); players who installed the mod find them in **Settings → Texture packs** as `mod:<mod folder>/<folder>`. They are never sent over the network.

## Worlds and mods

A saved world remembers the packs it was played with (`world.json`, `packs`). Opening it without one of them logs a warning; blocks of the missing mod become air and its items disappear from inventories. Adding a mod to an existing world is safe; new ores and biomes only appear in chunks generated afterwards. Creatures, dropped items and falling blocks of a missing mod are left out of their chunk with one warning per name. Saved entities are a header and versioned components (decision E-420): data a later version of the game stores with an entity (a component or the newer fields of one) is kept when this version saves the world again, and data an older version stored is read with defaults for what it lacks, so new entity fields need no new world format.

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
| Textures | procedural (ore, gem, tools, beetle); the texture pack `texturepacks/rubies_hd` (64×, own specs with `detail`) |
| Structure | `ruby_shrine`: a demo of the showcase world (ruby block on a stone frame with roses and lanterns) |
| Translations | English and German, including handbook texts (`lang/*.handbook.json`) |

Screenshot: `docs/screenshots/07-mod-items.png` (all mod items in the inventory, the ruby recipe on offer). `tests/content/test_mods.cpp` loads it, checks every part and crafts with its recipes; `tests/gameplay/test_headless_cli.cpp` starts `qtcraft-headless --mods-dir mods`.

## Boundary between core and mods

Mods **can**: add and replace blocks (with states, shapes and light), shapes, building-part families and materials (and exclude materials from parts), items with tags, recipes, smelting recipes, fuels, machine blocks with their own categories, mobs, biomes, features, ores, terrain settings, structures, village styles and settings, showcase settings, textures, sounds and translations; depend on other mods.

Mods **cannot**: run code, add new kinds of behaviour (a new AI, a new block mechanic, new UI: every machine uses the furnace logic and screen, every storage block the chest screen, state rules are the fixed ones above), change the protocol, the save format or the rules the server enforces, or read and write files outside their own pack directory. Everything a mod provides goes through `QcContentPackLoader` (the only code that reads pack files) into `QcContentRegistry`; the rest of the game only sees the registry. The server sends the registry to clients, so clients never read mod files themselves.

Only regular files and real directories are read. A linked mod folder is reported as not loaded; linked sub-folders and files and special files such as pipes or devices are not read (silently inside the content folders, with a warning for `pack.json` and `worldgen/terrain.json`). Size limits: 4 MiB per file, 256 mod directories (64 loaded), at most 64 drops per block, at most 64 `plantOn` entries per sapling, manifest `name` 256 and `version` 64 characters; the parser checks every field's type and range. All mods together may make the loader read at most 64 MiB: the files of a mod are measured before anything is read (each file counts at least 256 bytes, a hard link as often as it appears), and every attempt counts, also for mods that are refused afterwards or loaded a second time. Each mod is checked right after loading, exactly as its content would reach clients (including the items created for its blocks): the counts clients accept and the 32 MiB content sync. Translations: a language file holds at most 65,536 entries (a larger one is skipped with a warning), all languages of the base game and all mods together at most 262,144 (the base game uses about 2,600); a language file that would go beyond that is skipped with a warning, and a mod whose generated part names would is refused (decision E-371; files are checked before part names exist, so a mod just under the limit can still be refused once its parts are named). Skipped files appear as warnings in the log like other broken files (`content warning:` on stderr for `qtcraft-headless`); the Mods screen shows only why a mod was not loaded. A mod that fails is refused and leaves no trace; at most 64 mods are loaded and checked per start (refused ones count too). Shaped recipes have at most 9 keys; procedural textures keep only `pattern`, `primary`, `secondary` (at most 32 characters each), `variation`, `seed`, `detail` (only `grain`, `bevel` in 0–1 and `smooth`) and an emblem's `bitmap` (16 × 16) and `palette` (16 colours). A final read-back like a client's is the safety net: should it ever fail, the game starts without mods. World generation is limited to 256 ore definitions with at most 4,096 ore blocks per chunk in total, 256 biomes, 1,024 features and 4,096 feature placements (at most 64 per biome) and 64 pool attempts per chunk. PNG textures may be at most 128 × 128 pixels (checked before decoding). Sounds are limited to the ranges above (8 layers of at most 10 s, 8 variants), checked by the parser and again by every client; a client renders at most 16 Mi samples (64 MiB) of sound, sounds beyond that stay silent. A mod that fails a check is skipped together with its dependents; the Mods screen shows the reason after the next world start. Security questions that go beyond that (sandboxing, signatures, permissions for future script hooks) are collected in `docs/Mod-Sicherheit.md`.
