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
| Server | `QcServer` (20 Hz tick timer), world generation |
| Mesh workers | `QcChunkMesher` on immutable chunk snapshots (`QcWorkerPool`) |

Data crosses threads only as immutable snapshots (`std::shared_ptr<const QcChunk>`, encoded frames) through `std::mutex`-guarded mailboxes; Qt queued calls only wake the receiver (see decision E-010).

## Protocol

Frames are `[quint16 type][QDataStream payload]`, see `QcProtocol.h` and decision E-007. Version: `QcProtocol::version` (currently 1).

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

A network transport only has to implement `QcTransport` (whole frames in both directions, e.g. length-prefixed over TCP) and accept connections into `QcServer::addClient`; nothing else changes.

## Content

Content packs are directories with `pack.json` (`id`, `name`, `version`, `dependencies`) and the folders `blocks/`, `textures/`, `lang/`. Names are `namespace:path`; unqualified names get the pack id as namespace. Block JSON fields: `name`, `translation`, `render` (`opaque`, `cutout`, `translucent`, `invisible`), `solid`, `opaque`, `liquid`, `replaceable`, `lightOpacity` (0–15), `hardness` (seconds by hand, negative = unbreakable), `tool`, `toolLevel`, `drops` (`[{item, count}]`), `textures` (`all`, `side`, `top`, `bottom`, or per face `east`/`west`/`south`/`north`). Procedural textures: `name`, `pattern` (`noise`, `speckle`, `planks`, `bricks`, `log_side`, `log_top`, `leaves`, `glass`, `grass_side`, `ore`, `water`, `tool`, `lump`), `primary`, `secondary` (`#RRGGBB[AA]`), `variation`, `seed`; PNG files in `textures/` are used as they are. `lang/<locale>.json` maps translation keys to text.

Runtime block ids are assigned in registration order and never stored; persistent data uses names (*planned:* save format).

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

All own targets are compiled with `-Wall -Wextra -Wpedantic -Werror -fno-exceptions` (MSVC: `/W4 /WX /EHs-c-`) through the `qc_project_options` interface target. Qt headers are system includes.
