# QtCraft architecture

This document describes the current state. Sections marked *planned* are not implemented yet.

## Modules

| Target | Layer | Depends on | Content |
|--------|-------|-----------|---------|
| `qc_common` | Common | QtCore | Coordinates, world constants. *Planned:* block/content registry, chunk storage, protocol messages. |
| `qc_server` | Server | `qc_common`, QtConcurrent | *Planned:* authoritative world, generator, physics, mobs, persistence, mod loader. |
| `qc_clientcore` | Client (GUI-free) | `qc_common`, QtGui (`QImage` only) | Procedural texture synthesis, chunk mesh format. *Planned:* world mirror, mesher, prediction, control interface. |
| `qtcraft` | Client (UI) | all of the above, Qt Quick | QML UI, `QcWorldView`/`QcWorldRenderer` (QRhi), camera, screenshot hook. |

The client never includes server headers; the only path between them is the protocol (*planned*).

## Rendering

`QcWorldView` is a `QQuickRhiItem`. GUI-thread setters (camera, atlas, mesh submissions) are copied to the render thread in `QcWorldRenderer::synchronize()`. Meshes are immutable `std::shared_ptr<const QcChunkMesh>` snapshots. Vertex format: position (world space, float), atlas UV, shade (face shading × ambient occlusion), sky light. Two pipelines: opaque with alpha test, translucent (blended, no depth write, sorted back to front per section). Shaders: `src/app/shaders/block.{vert,frag}` (GLSL 440, compiled for all RHI backends by `qt_add_shaders`).

## Build options and test hooks

| Option | Effect |
|--------|--------|
| CMake presets `debug`, `release`, `asan` (Address + UndefinedBehavior), `tsan` (Thread) | Separate build directories under `build/<preset>` |
| `QC_SANITIZERS` | Sanitizer list applied to all own targets |
| `QTCRAFT_SCREENSHOT=<file.png>` | Saves a screenshot after `QTCRAFT_SCREENSHOT_FRAMES` (default 90) frames and quits |
| `QTCRAFT_LANGUAGE=<locale>` | Overrides the UI language (e.g. `de`) |

All own targets are compiled with `-Wall -Wextra -Wpedantic -Werror -fno-exceptions` (MSVC: `/W4 /WX /EHs-c-`) through the `qc_project_options` interface target. Qt headers are system includes.
