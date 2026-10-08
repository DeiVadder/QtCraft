# QtCraft architecture

This document describes the current state. Sections marked *planned* are not implemented yet.

## Modules

| Target | Layer | Depends on | Content |
|--------|-------|-----------|---------|
| `qc_common` | Common | QtCore | Coordinates, chunk storage (palette sections), content registry and JSON parser, protocol messages and codec, transports (`QcTransport`, `QcInProcessTransport`, `QcConnection`), shared physics, thread hand-over helpers (`QcMailbox`, `QcWorkerPool`). |
| `qc_base_content` | Data | – | Base game content pack embedded as `:/content/qtcraft`. |
| `qc_server` | Server | `qc_common`, `qc_base_content` | `QcServer` (authoritative tick loop, players, chunk streaming), `QcMachineManager`, `QcHunger`, `QcServerWorld`, world generators, content-pack loader, mod loader (`QcModLoader`), `QcLocalServerHost` (server thread for single player). |
| `qc_clientcore` | Client (GUI-free) | `qc_common`, QtGui (`QImage` only) | `QcClientSession` (world mirror, prediction), `QcChunkMesher`, `QcMeshScheduler`, texture atlas, camera, `QcControlInterface`. |
| `qtcraft` | Client (UI) | `qc_server`, `qc_clientcore`, Qt Quick | QML UI, `QcGameController`, `QcWorldView`/`QcWorldRenderer` (QRhi), screenshot hook. Links the server only to host it in-process. |
| `qtcraft-headless` | Tool | `qc_server`, `qc_clientcore` | Server plus control-interface client without a window. |

The client code never includes server headers except in the two composition roots (`QcGameController` and `QcHeadlessGame`), which only create the `QcLocalServerHost` and take a transport from it. All game interaction goes through protocol messages.

## Threads

| Thread | Runs |
|--------|------|
| GUI | QML, `QcGameController`, `QcClientSession` (20 Hz tick timer), mesh scheduling decisions |
| Render (Qt Quick) | `QcWorldRenderer` |
| Server | `QcServer` (20 Hz tick timer) |
| Generation workers | `QcTerrainGenerator::generate` (`QcWorkerPool` in `QcServerWorld`) |
| Mesh workers | `QcChunkMesher` on immutable chunk snapshots (`QcWorkerPool`) |

Data crosses threads only as immutable snapshots (`std::shared_ptr<const QcChunk>`, encoded frames) through `std::mutex`-guarded mailboxes; Qt queued calls only wake the receiver (see decision E-010).

## Protocol

Frames are `[quint16 type][QDataStream payload]`, see `QcProtocol.h` and decision E-007. Version: `QcProtocol::version` (currently 12). Each peer only accepts the message types the other side may send. Chunk sections are a palette plus run-length encoded indices (E-016).

| Message | Direction | Content |
|---------|-----------|---------|
| `Hello` | C→S | protocol version, player name, appearance (outfit, hair, four colours) |
| `Welcome` | S→C | protocol version, entity id, world name, seed |
| `ContentSync` | S→C | complete content registry (incl. the list of loaded packs); runtime block ids = registry indices |
| `ChunkData` | S→C | one chunk column (compressed palette sections) |
| `ChunkUnload` | S→C | chunk position |
| `BlockUpdate` | S→C | position, block id |
| `PlayerInput` | C→S | sequence, movement intent, view direction |
| `PlayerState` | S→C | last processed input sequence, authoritative motion |
| `Disconnect` | both | reason |
| `StartMining`, `CancelMining`, `FinishMining` | C→S | block position |
| `UseItem` | C→S | clicked block and face |
| `SelectHotbarSlot`, `MoveInventoryItem` | C→S | slot numbers (36–44: crafting grid, 45–47: open machine input, fuel, output), count |
| `ChatInput` / `ChatMessage` | C→S / S→C | text |
| `InventoryState` | S→C | all 36 stacks, selected slot, crafting grid, grid size, result |
| `OpenCrafting` | S→C | grid size (3 at a crafting table) |
| `TakeCraftingResult`, `CloseCrafting` | C→S | – |
| `CraftRecipe` | C→S | recipe name, count (recipe book) |
| `ConsumeItem` | C→S | – (eat the held item) |
| `EntitySpawn`, `EntityMoves`, `EntityRemove` | S→C | creatures and other players within 64 m, dropped items within 32 m (`mobType` −2 with the item stack; sent again when the count changes, moves only when the item moved); players' appearance, view pitch, state flags (swing started, on ground, in water, sneaking, sprinting) and held item; `EntityRemove.died` for deaths |
| `PlayerHealth` | S→C | health, maximum, dead |
| `WorldTime` | S→C | seconds, day length |
| `AttackEntity`, `Respawn` | C→S | target id / – |
| `DropItem` | C→S | whole stack or one item of the selected slot |
| `SetAppearance` | C→S | new appearance (re-sent to other clients) |
| `OpenMachine` | S→C | position and category of the machine the player opened |
| `MachineState` | S→C | input, fuel and output stack, share of fuel left and of the current item done |
| `CloseMachine` | both | the screen closed (client) / the machine closed (server: out of reach, broken, death) |
| `PlayerFood` | S→C | food points, maximum |

A network transport only has to implement `QcTransport` (whole frames in both directions, e.g. length-prefixed over TCP) and accept connections into `QcServer::addClient`; nothing else changes.

## Content

Content packs are directories with `pack.json` (`id`, `name`, `version`, `dependencies`) and the folders `blocks/`, `textures/`, `lang/`. Names are `namespace:path`; unqualified names get the pack id as namespace. Block JSON fields: `name`, `translation`, `render` (`opaque`, `cutout`, `translucent`, `invisible`), `solid`, `opaque`, `liquid`, `replaceable`, `lightOpacity` (0–15), `hardness` (seconds by hand, negative = unbreakable), `tool`, `toolLevel`, `drops` (`[{item, count, chance}]`), `crafting` (crafting table), `machine` (`category`, `speed`), `fuelSeconds`, `shape` (`cube`, `cross`), `sapling` (`feature`, `minSeconds`, `maxSeconds`, `plantOn`, `plantChance`), `textures` (`all`, `side`, `top`, `bottom`, or per face `east`/`west`/`south`/`north`). Procedural textures: `name`, `pattern` (`noise`, `speckle`, `planks`, `bricks`, `log_side`, `log_top`, `leaves`, `glass`, `grass_side`, `ore`, `water`, `tool`, `lump`, `pickaxe`, `axe`, `shovel`, `sword`, `stick`, `ingot`, `gem`, `sapling`, `face`, `furnace`), `primary`, `secondary` (`#RRGGBB[AA]`), `variation`, `seed`; PNG files in `textures/` are used as they are. `lang/<locale>.json` maps translation keys to text.

World generation data (server only, not sent to clients): `biomes/*.json` (`name`, `temperature`, `humidity` in 0..1, `baseHeight`, `heightVariation`, `surface`, `subsurface`, `subsurfaceDepth`, `underwater`, `beach`, `frozenWater`, `snowLine`, `snow`, `features: [{feature, density}]`), `features/*.json` (`type` `tree`/`column`, `trunk`, `leaves`, `minHeight`, `maxHeight`, `shape` `round`/`cone`, `ground`), `ores/*.json` (`block`, `replaces`, `minY`, `maxY`, `veinsPerChunk`, `veinSize`), `worldgen/terrain.json` (`seaLevel`, `stone`, `water`, `bedrock`, `caves`, `caveTunnelWidth`, `caveCavernThreshold`). A later pack replaces entries with the same name.

Character content (sent to clients): `models/*.json` (`name`, `bones`: `name`, `role` head/body/right_arm/left_arm/right_leg/left_leg/none, `pivot`, `boxes` with `from`, `to`, `slot`), `appearance/*.json` (`name`, `kind` outfit/hair, `layers`: slot → `texture` and `tint` skin/hair/shirt/pants/none, hair `boxes`); mobs may use `skeleton`, `outfit` and `armsForward` instead of `model` boxes (E-032).

Runtime block ids are assigned in registration order and never stored; persistent data uses names.

Mods are content packs in the mods directory (`<data root>/mods`, `--mods-dir`); `QcModLoader` loads the base game first and then every mod after its dependencies, skipping broken ones. The base game goes through exactly the same loader. Details, the boundary between core and mods and the file formats are in `docs/Modding.md`; open security questions in `docs/Mod-Sicherheit.md`.

## Persistence

A saved world is a directory below `<app data>/worlds/<folder>` (`QTCRAFT_DATA_DIR` overrides the base; `QcWorldLibrary` lists, creates and deletes worlds). `QcWorldStorage` reads and writes:

| File | Content |
|------|---------|
| `world.json` | `formatVersion`, `name`, `seed` (string, 64-bit), `created`, `lastPlayed`, `worldTime`, `packs` (`id`, `name`, `version` of the base game and mods in use), `gameRules` (`keepInventory`) |
| `regions/r.<rx>.<rz>.qcr` | Modified chunks of a 32 × 32 chunk region: block-name palette + chunks (palette sections, run-length indices) |
| `players/<name>.qcp` | Position, velocity, view direction, health, selected slot, inventory (item names, counts, damage), food points and exhaustion (format 2; format 1 files load with full food) |
| `entities.qce` | Dropped items (name, count, damage, position, velocity, age, pickup delay; waiting drops included) and growing saplings (position, seconds left) |
| `machines.qce` | Machines (position, input, fuel and output stack by item name, fuel left and its full time, progress) |

When a world is opened with a different set of packs than it was saved with, the server logs a warning per missing or changed pack (`QcServer::openWarnings()`); blocks of a missing mod load as air and its items are dropped from inventories.

Binary files share a container: magic (`QCRG`/`QCPL`/`QCEN`/`QCMA`), format version (per file type; older versions stay readable), payload size, CRC-32, payload. Loading validates every part; a damaged file stops the world from opening with a message naming the file. Unknown block or item names (e.g. after removing a mod) become air or are dropped, with a warning. Only modified chunks are stored; they stay in memory while the world is open and their regions are rewritten (atomically via `QSaveFile`) on `/save`, every 60 s and when the server stops. Everything else is regenerated from the seed.

## Interaction

The client sends intentions; the server decides (`QcServer`):

- **Mining:** `StartMining`/`CancelMining`/`FinishMining`. The server checks reach (5 m + 1 m slack), the target and the elapsed ticks against `QcMining::breakSeconds` (hardness × 1.5 s, divided by the tool speed with the right tool; × 5 s without a required tool, which also gives no drops). Drops (each with its `chance`) fall into the world as item entities (E-028); tools lose durability. Plants (cross-shaped blocks) on top of a broken block break with it.
- **Placing:** `UseItem` on a block face places the held item's block into the neighbouring cell if it is replaceable, within reach and not inside a player. Saplings need one of their `plantOn` blocks below, other cross-shaped blocks a solid block.
- **Dropped items** (`QcItemEntityManager`, E-028): physics through `QcPhysics::stepEntity` with weak air drag, floating in water, resting items skipped until their ground changes; merging of equal stacks within 1 m every 10 ticks; pickup when touching a living player after 0.5 s (thrown: 2 s), partial when the inventory is full; ageing only in loaded chunks, despawn after 300 s; at most 512 awake items (when a chunk loads with more, the youngest sleep in place: frozen, not sent, same id, woken oldest first), a bounded queue for further drops that appear only in loaded chunks, 4096 items in the world; stacks above `maxStack` are split; moves are sent once an item is 2 cm from its last sent position. `DropItem` (key Q, Ctrl+Q for the stack) throws items from the eyes along the view direction. On death without `keepInventory` (E-029) inventory and grid scatter as items.
- **Plants** (`QcPlantManager`, E-030, E-031): leaves of tree features decay a few at a time once no trunk is within four blocks (checked after a trunk is removed); saplings grow into their feature after a random time while loaded, using the generator's tree shapes (`QcTreeShape`); a despawning sapling item plants itself with its `plantChance`.
- **Inventory:** 36 slots (9 hotbar). The server sends the full `InventoryState` after every change; `MoveInventoryItem` merges or swaps stacks.
- **Chat and commands:** `ChatInput`/`ChatMessage`; commands `/help`, `/give <item> [count]`, `/save`, and from stage 5 `/time`, `/summon`, `/kill`, `/heal`, from stage 9 `/gamerule keepInventory`, `/spawnitem`, `/setblock`, from stage 11 `/feed [food]` (all but `/help`, `/save` and reading a game rule need `allowCheats`, E-021).
- **Eating and hunger** (`QcHunger`, E-039): `ConsumeItem` eats one of the held item if it has `food` and the player is not full; the item's `food` points are added (at most 20). Walking, sprinting, swimming, jumping, mining, attacking and taking damage add exhaustion; every 4 points cost one food point. At 18 food or more the player heals one point every 4 s (3 exhaustion each); at 0 food they lose one point every 4 s down to 1. Respawning refills the food. Using a machine or crafting table opens it even with food in hand.
- **Machines** (`QcMachineManager`, E-037, E-038): using a block with `machine` opens it (sneaking places against it) within 5 m + 1 m slack; the server closes it when the player dies, walks away or the block goes. Slots 45–47 of `MoveInventoryItem` are the open machine's input, fuel (fuel only) and output (take only). A machine lights one fuel item when its input has a recipe of its category and the result fits; burning and progress run at the machine's `speed`, without fuel the progress falls back twice as fast. Only machines in loaded chunks tick, at most 4096 exist; breaking or replacing the block drops the contents as items.

Every visible, non-liquid block automatically gets an item of the same name that places it (`QcContentRegistry::createBlockItems`); `items/*.json` defines further items (`name`, `translation`, `texture`, `maxStack`, `places`, `tool`, `toolLevel`, `toolSpeed`, `durability`, `attackDamage`, `food` in food points, `fuelSeconds`); block items inherit the block's `fuelSeconds`.

UI controls: left mouse mines or attacks, F mines, right mouse / R places, eats or opens a crafting table or furnace, Q throws one item (Ctrl+Q the stack), 1–9 and the wheel select the hotbar slot, E opens the inventory, T or Enter the chat, Esc the pause menu (save, quit to menu).

## Crafting

- **Recipes** come from `recipes/*.json`: `name`, `type` (`shaped`, the default, or `shapeless`), for shaped recipes `pattern` (up to 3 strings of up to 3 characters, space = empty) and `key` (one character → item), for shapeless ones `ingredients` (1–9 item names), and `result` (`item`, `count`). Patterns are trimmed to their bounding box when parsed. `QcCrafting` (shared by server and client) matches a square grid against all recipes: shaped recipes at any offset and mirrored horizontally, shapeless ones as a multiset. A recipe needs a 3 × 3 grid if its shape or ingredient count does not fit 2 × 2.
- **Grid:** the inventory screen has a 2 × 2 grid; using a block with `"crafting": true` (crafting table) opens 3 × 3 as long as the player stays within 5 m. Grid cells are slots 36–44 (three per row) and live on the server; `MoveInventoryItem` moves, splits and merges between inventory and grid. `TakeCraftingResult` consumes one item per used cell and adds the result if it fits. `CloseCrafting` puts the grid back into the inventory; what does not fit is thrown on the ground (also when the player disconnects).
- **Recipe book:** `CraftRecipe` crafts directly from the inventory, round by round, each round all-or-nothing on a copy of the inventory. 3 × 3 recipes need a crafting table within 5 m. The inventory screen lists the recipes that can be made now; the control interface offers `recipes` and `craft`.
- **Base recipes:** planks from each log, sticks, crafting table, furnace (8 cobblestone), wooden/stone/iron/diamond pickaxe, axe, shovel and sword, sandstone.
- **Smelting** (E-037): `smelting/*.json` (`name`, `category` default `smelting`, `input`, `result` with `item`/`count`, `seconds` default 10). Base recipes in the furnace: iron and gold ore to ingots, sand to glass, clay to bricks, cobblestone to stone, logs to charcoal, raw to cooked porkchop. Fuel: coal and charcoal 80 s, logs, planks and crafting tables 15 s, wooden tools 10 s, sticks and saplings 5 s. The furnace screen (`FurnaceScreen.qml`) shows the three slots, a flame for the fuel left and an arrow for the progress.

## Creatures, health and time

- **Mobs** come from `mobs/*.json`: `name`, `translation`, `health`, `width`, `height`, `walkSpeed`, `chaseSpeed`, `hostile`, `attackDamage`, `attackRange`, `attackCooldown`, `sightRange`, `spawn` (`time` `day`/`night`/`any`, `blocks`, `maxGroup` 1–4, `weight`, `caves`; night creatures spawned in caves stay by day), `model` (boxes `from`/`to` in metres around the feet, facing north, with a `texture`) and `drops`. Base mobs: zombie (hostile, night and caves), slime (hostile, caves), pig (passive, day).
- **Server** (`QcMobManager`): every second up to four spawn attempts per player in a ring of 20–48 m, on the surface (by time of day) or on cave floors at least 8 blocks below the surface; caps of 8 hostile and 6 passive mobs per player. Behaviour per tick: hostile mobs chase the nearest player in sight and strike in range with a cooldown; passive mobs wander and flee when hurt; jumping over one-block steps; swimming. Mobs despawn beyond 96 m, night mobs by day once nobody is near. Mobs are not saved.
- **Combat:** `AttackEntity` (reach 4 m plus the target's size, 0.4 s cooldown, damage from the held item's `attackDamage`, 1 by hand), 0.5 s invulnerability after a hit, knock-back both ways. Loot falls where the mob died.
- **Health:** 20 points; fall damage of one point per metre beyond 3 m; regeneration of one point every 4 s while food is at least 18 (see Eating and hunger); death shows a screen and waits for `Respawn` (back to the spawn point with full health; the inventory is kept or dropped depending on `keepInventory`). Health is saved with the player.
- **Time:** a day lasts 600 s (`QcDayCycle`); the server keeps `worldTime` (saved in `world.json`) and sends `WorldTime` every second; the client advances it between updates and derives sky colour, fog and the daylight factor of the block shader.
- **Client:** `QcClientSession` mirrors entities (`EntitySpawn`/`EntityMoves`/`EntityRemove`, interpolated between ticks) and finds the creature under the crosshair; `QcEntityMesher` turns the boxes into triangles every frame and draws dropped items as small spinning, bobbing cubes (cube blocks) or crossed sprites (other items), up to three copies for larger stacks; the HUD shows hearts, the food bar and the time of day.

## World generation

`QcTerrainGenerator` (decision E-014) is deterministic per seed and thread-safe. The server generates chunks on a `QcWorkerPool` (`QcServerWorld::requestChunk`), nearest first with a bounded queue, and collects finished chunks every tick; chunks no player needs are unloaded. Sky light is computed client-side while meshing (decision E-015).

## Rendering

`QcWorldView` is a `QQuickRhiItem`. GUI-thread setters (camera, atlas, mesh submissions) are copied to the render thread in `QcWorldRenderer::synchronize()`. Meshes are immutable `std::shared_ptr<const QcChunkMesh>` snapshots. Vertex format: position (world space, float), atlas UV, shade (face shading × ambient occlusion), sky light. Cross-shaped blocks become two double-sided diagonal quads. Entities and the first-person hand use their own vertex format with a tint colour and the shaders `entity.{vert,frag}`; the hand is drawn last with `hand.vert`, which squeezes its depth to the front (E-034). Skeleton models are meshed every frame by `QcEntityMesher` from `QcAnimator` poses (E-033); the camera mode and its wall collision come from `QcViewCamera` (E-035). Block pipelines: opaque with alpha test, translucent (blended, no depth write, sorted back to front per section). Shaders: `src/app/shaders/block.{vert,frag}` (GLSL 440, compiled for all RHI backends by `qt_add_shaders`).

## Build options and test hooks

| Option | Effect |
|--------|--------|
| CMake presets `debug`, `release`, `asan` (Address + UndefinedBehavior), `tsan` (Thread) | Separate build directories under `build/<preset>` |
| `QC_SANITIZERS` | Sanitizer list applied to all own targets |
| `QTCRAFT_SCREENSHOT=<file.png>` | Saves a screenshot after `QTCRAFT_SCREENSHOT_FRAMES` (default 90) frames and quits; with `--play` it waits until the world is fully loaded and meshed |
| `QTCRAFT_BENCHMARK=<seconds>` (with `--play`) | After a 3 s warm-up the player sprints and jumps straight ahead; frame times are recorded and logged (average, 99th percentile, worst), then the game quits ; with `QTCRAFT_BENCHMARK_SCENARIO=stand` the player stands still. A `BENCH ...` line adds server tick times and peak memory for `scripts/bench.sh` |
| `QTCRAFT_LANGUAGE=<locale>`, `--language <locale>` | Overrides the UI language (e.g. `de`) |
| `--play`, `--world <name>`, `--seed <seed>` | Start a single-player world immediately |
| `--look <yaw,pitch>` | Initial view direction (for reproducible screenshots) |
| `--view-distance <chunks>` | View distance (2–16, default 8) |
| `--spawn <x,z>` | Spawn above this block column (reproducible scenes) |
| `--chat <text>` (repeatable), `--overlay <inventory\|pause\|settings\|character\|chat\|furnace>` | Sent / opened once the world is loaded (reproducible UI screenshots); `furnace` uses the block in view after 1 s, moves hotbar slots 1 and 2 into its input and fuel and lets the screenshot wait 2.5 s more; without a machine in view the screenshot is taken after 6 s with a warning. Example: `--look 0,-89 --chat "/give iron_ore 5" --chat "/give coal 2" --chat "/setblock ~ ~-1 ~ furnace" --overlay furnace` |
| `QTCRAFT_DATA_DIR=<dir>` | Base directory for saved worlds (`worlds/`) and mods (`mods/`); tests set it to `build/<preset>/test-data` |
| `--mods-dir <dir>`, `--no-mods` (game and `qtcraft-headless`) | Load mods from another directory / only the base game |
| `qtcraft-headless --map <png> [--map-radius <blocks>]` | Top-down terrain map of a seed |

All own targets are compiled with `-Wall -Wextra -Wpedantic -Werror -fno-exceptions` (MSVC: `/W4 /WX /EHs-c-`) through the `qc_project_options` interface target. Qt headers are system includes.
