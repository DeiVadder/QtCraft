# QtCraft architecture

This document describes the current state. Sections marked *planned* are not implemented yet.

## Modules

| Target | Layer | Depends on | Content |
|--------|-------|-----------|---------|
| `qc_common` | Common | QtCore | Coordinates, chunk storage (palette sections), content registry and JSON parser, protocol messages and codec, transports (`QcTransport`, `QcInProcessTransport`, `QcConnection`), shared physics, thread hand-over helpers (`QcMailbox`, `QcWorkerPool`). |
| `qc_base_content` | Data | – | Base game content pack embedded as `:/content/qtcraft`. |
| `qc_server` | Server | `qc_common`, `qc_base_content` | `QcServer` (authoritative tick loop, players, chunk streaming), `QcServerWorld`, world generators, content-pack loader, `QcLocalServerHost` (server thread for single player). |
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

Frames are `[quint16 type][QDataStream payload]`, see `QcProtocol.h` and decision E-007. Version: `QcProtocol::version` (currently 8). Each peer only accepts the message types the other side may send. Chunk sections are a palette plus run-length encoded indices (E-016).

| Message | Direction | Content |
|---------|-----------|---------|
| `Hello` | C→S | protocol version, player name |
| `Welcome` | S→C | protocol version, entity id, world name, seed |
| `ContentSync` | S→C | complete content registry; runtime block ids = registry indices |
| `ChunkData` | S→C | one chunk column (compressed palette sections) |
| `ChunkUnload` | S→C | chunk position |
| `BlockUpdate` | S→C | position, block id |
| `PlayerInput` | C→S | sequence, movement intent, view direction |
| `PlayerState` | S→C | last processed input sequence, authoritative motion |
| `Disconnect` | both | reason |
| `StartMining`, `CancelMining`, `FinishMining` | C→S | block position |
| `UseItem` | C→S | clicked block and face |
| `SelectHotbarSlot`, `MoveInventoryItem` | C→S | slot numbers (36–44: crafting grid), count |
| `ChatInput` / `ChatMessage` | C→S / S→C | text |
| `InventoryState` | S→C | all 36 stacks, selected slot, crafting grid, grid size, result |
| `OpenCrafting` | S→C | grid size (3 at a crafting table) |
| `TakeCraftingResult`, `CloseCrafting` | C→S | – |
| `CraftRecipe` | C→S | recipe name, count (recipe book) |
| `ConsumeItem` | C→S | – (eat the held item) |
| `EntitySpawn`, `EntityMoves`, `EntityRemove` | S→C | creatures and other players within 64 m |
| `PlayerHealth` | S→C | health, maximum, dead |
| `WorldTime` | S→C | seconds, day length |
| `AttackEntity`, `Respawn` | C→S | target id / – |

A network transport only has to implement `QcTransport` (whole frames in both directions, e.g. length-prefixed over TCP) and accept connections into `QcServer::addClient`; nothing else changes.

## Content

Content packs are directories with `pack.json` (`id`, `name`, `version`, `dependencies`) and the folders `blocks/`, `textures/`, `lang/`. Names are `namespace:path`; unqualified names get the pack id as namespace. Block JSON fields: `name`, `translation`, `render` (`opaque`, `cutout`, `translucent`, `invisible`), `solid`, `opaque`, `liquid`, `replaceable`, `lightOpacity` (0–15), `hardness` (seconds by hand, negative = unbreakable), `tool`, `toolLevel`, `drops` (`[{item, count}]`), `textures` (`all`, `side`, `top`, `bottom`, or per face `east`/`west`/`south`/`north`). Procedural textures: `name`, `pattern` (`noise`, `speckle`, `planks`, `bricks`, `log_side`, `log_top`, `leaves`, `glass`, `grass_side`, `ore`, `water`, `tool`, `lump`), `primary`, `secondary` (`#RRGGBB[AA]`), `variation`, `seed`; PNG files in `textures/` are used as they are. `lang/<locale>.json` maps translation keys to text.

World generation data (server only, not sent to clients): `biomes/*.json` (`name`, `temperature`, `humidity` in 0..1, `baseHeight`, `heightVariation`, `surface`, `subsurface`, `subsurfaceDepth`, `underwater`, `beach`, `frozenWater`, `snowLine`, `snow`, `features: [{feature, density}]`), `features/*.json` (`type` `tree`/`column`, `trunk`, `leaves`, `minHeight`, `maxHeight`, `shape` `round`/`cone`, `ground`), `ores/*.json` (`block`, `replaces`, `minY`, `maxY`, `veinsPerChunk`, `veinSize`), `worldgen/terrain.json` (`seaLevel`, `stone`, `water`, `bedrock`, `caves`, `caveTunnelWidth`, `caveCavernThreshold`). A later pack replaces entries with the same name.

Runtime block ids are assigned in registration order and never stored; persistent data uses names (*planned:* save format).

## Persistence

A saved world is a directory below `<app data>/worlds/<folder>` (`QTCRAFT_DATA_DIR` overrides the base; `QcWorldLibrary` lists, creates and deletes worlds). `QcWorldStorage` reads and writes:

| File | Content |
|------|---------|
| `world.json` | `formatVersion`, `name`, `seed` (string, 64-bit), `created`, `lastPlayed`, `worldTime` |
| `regions/r.<rx>.<rz>.qcr` | Modified chunks of a 32 × 32 chunk region: block-name palette + chunks (palette sections, run-length indices) |
| `players/<name>.qcp` | Position, velocity, view direction, health, selected slot, inventory (item names, counts, damage) |

Binary files share a container: magic (`QCRG`/`QCPL`), format version, payload size, CRC-32, payload. Loading validates every part; a damaged file stops the world from opening with a message naming the file. Unknown block or item names (e.g. after removing a mod) become air or are dropped, with a warning. Only modified chunks are stored; they stay in memory while the world is open and their regions are rewritten (atomically via `QSaveFile`) on `/save`, every 60 s and when the server stops. Everything else is regenerated from the seed.

## Interaction

The client sends intentions; the server decides (`QcServer`):

- **Mining:** `StartMining`/`CancelMining`/`FinishMining`. The server checks reach (5 m + 1 m slack), the target and the elapsed ticks against `QcMining::breakSeconds` (hardness × 1.5 s, divided by the tool speed with the right tool; × 5 s without a required tool, which also gives no drops). Drops go straight into the inventory; tools lose durability.
- **Placing:** `UseItem` on a block face places the held item's block into the neighbouring cell if it is replaceable, within reach and not inside a player.
- **Inventory:** 36 slots (9 hotbar). The server sends the full `InventoryState` after every change; `MoveInventoryItem` merges or swaps stacks.
- **Chat and commands:** `ChatInput`/`ChatMessage`; commands `/help`, `/give <item> [count]`, `/save`, and from stage 5 `/time`, `/summon`, `/kill`, `/heal` (all but `/help` and `/save` need `allowCheats`, E-021).
- **Eating:** `ConsumeItem` eats one of the held item if it has `food` and the player is hurt.

Every visible, non-liquid block automatically gets an item of the same name that places it (`QcContentRegistry::createBlockItems`); `items/*.json` defines further items (`name`, `translation`, `texture`, `maxStack`, `places`, `tool`, `toolLevel`, `toolSpeed`, `durability`, `attackDamage`, `food`).

UI controls: left mouse mines or attacks, F mines, right mouse / R places or eats, 1–9 and the wheel select the hotbar slot, E opens the inventory, T or Enter the chat, Esc the pause menu (save, quit to menu).

## Crafting

- **Recipes** come from `recipes/*.json`: `name`, `type` (`shaped`, the default, or `shapeless`), for shaped recipes `pattern` (up to 3 strings of up to 3 characters, space = empty) and `key` (one character → item), for shapeless ones `ingredients` (1–9 item names), and `result` (`item`, `count`). Patterns are trimmed to their bounding box when parsed. `QcCrafting` (shared by server and client) matches a square grid against all recipes: shaped recipes at any offset and mirrored horizontally, shapeless ones as a multiset. A recipe needs a 3 × 3 grid if its shape or ingredient count does not fit 2 × 2.
- **Grid:** the inventory screen has a 2 × 2 grid; using a block with `"crafting": true` (crafting table) opens 3 × 3 as long as the player stays within 5 m. Grid cells are slots 36–44 (three per row) and live on the server; `MoveInventoryItem` moves, splits and merges between inventory and grid. `TakeCraftingResult` consumes one item per used cell and adds the result if it fits. `CloseCrafting` puts the grid back into the inventory (what does not fit is lost, E-019).
- **Recipe book:** `CraftRecipe` crafts directly from the inventory, round by round, each round all-or-nothing on a copy of the inventory. 3 × 3 recipes need a crafting table within 5 m. The inventory screen lists the recipes that can be made now; the control interface offers `recipes` and `craft`.
- **Base recipes:** planks from each log, sticks, crafting table, wooden/stone/iron/diamond pickaxe, axe, shovel and sword, glass, bricks, sandstone, and iron/gold ingots from ore plus coal (E-024).

## Creatures, health and time

- **Mobs** come from `mobs/*.json`: `name`, `translation`, `health`, `width`, `height`, `walkSpeed`, `chaseSpeed`, `hostile`, `attackDamage`, `attackRange`, `attackCooldown`, `sightRange`, `spawn` (`time` `day`/`night`/`any`, `blocks`, `maxGroup` 1–4, `weight`, `caves`; night creatures spawned in caves stay by day), `model` (boxes `from`/`to` in metres around the feet, facing north, with a `texture`) and `drops`. Base mobs: zombie (hostile, night and caves), slime (hostile, caves), pig (passive, day).
- **Server** (`QcMobManager`): every second up to four spawn attempts per player in a ring of 20–48 m, on the surface (by time of day) or on cave floors at least 8 blocks below the surface; caps of 8 hostile and 6 passive mobs per player. Behaviour per tick: hostile mobs chase the nearest player in sight and strike in range with a cooldown; passive mobs wander and flee when hurt; jumping over one-block steps; swimming. Mobs despawn beyond 96 m, night mobs by day once nobody is near. Mobs are not saved.
- **Combat:** `AttackEntity` (reach 4 m plus the target's size, 0.4 s cooldown, damage from the held item's `attackDamage`, 1 by hand), 0.5 s invulnerability after a hit, knock-back both ways. Loot goes into the killer's inventory.
- **Health:** 20 points; fall damage of one point per metre beyond 3 m; regeneration of one point every 4 s; death shows a screen and waits for `Respawn` (back to the spawn point with full health, inventory kept). Health is saved with the player.
- **Time:** a day lasts 600 s (`QcDayCycle`); the server keeps `worldTime` (saved in `world.json`) and sends `WorldTime` every second; the client advances it between updates and derives sky colour, fog and the daylight factor of the block shader.
- **Client:** `QcClientSession` mirrors entities (`EntitySpawn`/`EntityMoves`/`EntityRemove`, interpolated between ticks) and finds the creature under the crosshair; `QcEntityMesher` turns the boxes into triangles every frame; the HUD shows hearts and the time of day.

## World generation

`QcTerrainGenerator` (decision E-014) is deterministic per seed and thread-safe. The server generates chunks on a `QcWorkerPool` (`QcServerWorld::requestChunk`), nearest first with a bounded queue, and collects finished chunks every tick; chunks no player needs are unloaded. Sky light is computed client-side while meshing (decision E-015).

## Rendering

`QcWorldView` is a `QQuickRhiItem`. GUI-thread setters (camera, atlas, mesh submissions) are copied to the render thread in `QcWorldRenderer::synchronize()`. Meshes are immutable `std::shared_ptr<const QcChunkMesh>` snapshots. Vertex format: position (world space, float), atlas UV, shade (face shading × ambient occlusion), sky light. Two pipelines: opaque with alpha test, translucent (blended, no depth write, sorted back to front per section). Shaders: `src/app/shaders/block.{vert,frag}` (GLSL 440, compiled for all RHI backends by `qt_add_shaders`).

## Build options and test hooks

| Option | Effect |
|--------|--------|
| CMake presets `debug`, `release`, `asan` (Address + UndefinedBehavior), `tsan` (Thread) | Separate build directories under `build/<preset>` |
| `QC_SANITIZERS` | Sanitizer list applied to all own targets |
| `QTCRAFT_SCREENSHOT=<file.png>` | Saves a screenshot after `QTCRAFT_SCREENSHOT_FRAMES` (default 90) frames and quits; with `--play` it waits until the world is fully loaded and meshed |
| `QTCRAFT_LANGUAGE=<locale>`, `--language <locale>` | Overrides the UI language (e.g. `de`) |
| `--play`, `--world <name>`, `--seed <seed>` | Start a single-player world immediately |
| `--look <yaw,pitch>` | Initial view direction (for reproducible screenshots) |
| `--view-distance <chunks>` | View distance (2–16, default 8) |
| `--spawn <x,z>` | Spawn above this block column (reproducible scenes) |
| `--chat <text>` (repeatable), `--overlay <inventory\|pause\|chat>` | Sent / opened once the world is loaded (reproducible UI screenshots) |
| `QTCRAFT_DATA_DIR=<dir>` | Base directory for saved worlds |
| `qtcraft-headless --map <png> [--map-radius <blocks>]` | Top-down terrain map of a seed |

All own targets are compiled with `-Wall -Wextra -Wpedantic -Werror -fno-exceptions` (MSVC: `/W4 /WX /EHs-c-`) through the `qc_project_options` interface target. Qt headers are system includes.
