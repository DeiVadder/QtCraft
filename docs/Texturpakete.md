# Texture packs

A texture pack changes how blocks, items, creatures, the character (skin, clothes, armour, cape) and the item icons of the user interface look, without changing the game. Packs are purely client-side: on a server every player can use their own. QtCraft ships two kinds as examples (decisions E-297 to E-304):

| Pack | Kind | Resolution | What it shows |
|------|------|-----------|---------------|
| **QtCraft HD** (`builtin:qtcraft_hd_32`, `_64`, `_128`) | procedural | 32×, 64×, 128× | The game's own procedural textures painted at a higher resolution with more detail |
| **Storybook** (`builtin:storybook_32`) | image files | 32× | 62 plain PNG files: warm colours, drawn outlines, a few hand-placed details |
| **Rubies HD** (`mod:rubies/rubies_hd`, in the example mod) | procedural specs in a mod | 64× | A pack shipped inside a mod |

## Choosing packs

**Settings → Texture packs** lists every pack with its preview, resolution, licence and credits. Switch a pack on and it goes to the top; the arrows change the order of the active packs. The order is the priority: for every texture the first active pack that has it wins, then the next one, and finally the game's own 16×16 texture. Changes apply at once, also in a running world (the world keeps drawing until the new textures are ready). Problems of a pack (a damaged file, a wrong size) are listed under it in the player's language; the affected texture simply comes from the next pack.

At most 16 packs can be active at a time. Packs change everything drawn from textures; the parts of the interface drawn by the game itself (hearts, hunger, hotbar frame, crosshair, buttons) stay as they are.

For one run only: `QTCRAFT_TEXTURE_PACKS=builtin:qtcraft_hd_64,builtin:storybook_32` (or `none`). The choice is stored in `settings.ini` as the array `texturePacks/<n>/id`.

## Where packs live

| Place | Id |
|-------|----|
| Built into the game (`content/texturepacks/<folder>` in the source) | `builtin:<folder>` |
| `<data>/texturepacks/<folder>` (macOS: `~/Library/Application Support/QtCraft/QtCraft/texturepacks`; **Open texture pack folder** in the settings) | `user:<folder>` |
| `<mods>/<mod>/texturepacks/<folder>` of an installed mod | `mod:<mod>/<folder>` |

**Reload** in the settings reads the folders again after you changed files.

## Layout of a pack

```
my_pack/
  texturepack.json
  preview.png                      (optional)
  textures/
    qtcraft/
      stone.png                    image: replaces the texture qtcraft:stone
      oak_planks.json              procedural spec: replaces qtcraft:oak_planks
    rubies/
      ruby.png                     textures of mods work the same way
```

The texture name is `<folder below textures>:<file name without extension>`. The names are those of the content: the `name` of each file in `content/qtcraft/textures/*.json` (and in a mod's `textures/`). `qtcraft-headless --export-textures <dir>` writes all of them as a ready pack (see below). Only lower-case letters, digits, `_`, `-` and `.` are allowed in file names, and the suffix must be lower-case `.png` or `.json`; sub-folders below the namespace folder, links (also a linked `textures` folder) and other file types are reported and not read; hidden files are ignored.

## Manifest `texturepack.json`

```json
{
    "format": 1,
    "name": "My pack",
    "description": "What makes it special.",
    "resolution": 32,
    "license": "CC-BY-4.0",
    "credits": [
        { "author": "Ada", "license": "CC-BY-4.0", "source": "own work", "note": "stone and ores" }
    ],
    "procedural": false,
    "preview": "preview.png"
}
```

| Field | Meaning |
|-------|---------|
| `format` | Always `1` |
| `name` | Required, at most 64 characters |
| `description` | Optional, at most 1000 characters |
| `resolution` | Edge length of every texture in pixels: 16, 32, 64, 128, 256 or 512 |
| `license`, `credits` | Shown in the settings; at most 32 credits with `author`, `license`, `source`, `note`. Fill them in for material by others (see `docs/ASSETS.md`) |
| `procedural` | `true`: also paint every procedural texture of the game and of mods at `resolution` (how QtCraft HD works) |
| `preview` | Optional PNG in the pack folder, at most 512×512 (QtCraft's own previews are 4×2 tiles) |

An invalid or missing manifest makes the pack unusable; the settings show why.

## Image textures

One PNG per texture, exactly `resolution`×`resolution` pixels, RGBA. Fully transparent pixels are holes on cut-out blocks (`"render": "cutout"`: leaves, plants, torches), on items and on figures; partial transparency is kept where the game draws a block as translucent (glass, water, ice). Blocks of the opaque layer (the default: stone, logs, planks and the like) are drawn without alpha test (decision E-756), so a transparent pixel in their textures shows in its colour, not as a hole; a texture pack must keep those textures fully opaque. Faces of blocks, item icons and the body parts of figures use the same squares as the 16×16 originals, so the easiest start is an export at the resolution you want.

**Animation** (decision E-878): a vertical strip of frames (`resolution` wide, a multiple of it high, at most 32 frames, the first at the top) animates. The frames are shown at the frame time of the game's own texture when that one is animated (fire, lava, water: `frameTime` and `interpolate` of its definition), otherwise for 2 ticks (100 ms) each without blending; a strip of one frame keeps an animated texture still. `--export-textures` writes animated textures as such strips, a good start for your own frames. A procedural spec animates with the same fields as the content (`frames`, `frameTime`, `interpolate`, see below).

## Procedural textures

A `.json` file uses the same format as `content/*/textures/*.json` (`pattern`, `primary`, `secondary`, `variation`, `seed`, `bitmap`/`palette` for emblems; the pattern list is in `docs/Architektur.md`, section Content). It is painted at the pack's resolution. Above 16× the 16×16 pattern is enlarged with an edge-aware pixel-art filter and gets detail that only the higher resolution can show (decision E-299); each pattern has its own amounts, a definition can change them:

```json
"detail": { "grain": 0.08, "bevel": 0.25, "smooth": true }
```

| Field | Effect |
|-------|--------|
| `grain` (0–1) | Fine noise on the surface |
| `bevel` (0–1) | Light along edges at the top and left, shade at the bottom and right (also around cut-out shapes) |
| `smooth` | Round diagonal and curved outlines while enlarging; `false` keeps straight boards and bricks crisp |

`detail` also works in a mod's own texture definitions; at 16× it has no effect.

Animated textures (decision E-878) add three fields:

| Field | Effect |
|-------|--------|
| `frames` (1–32) | Pictures in the loop; 1 (or leaving it out) is a still texture |
| `frameTime` (1–255) | Ticks (50 ms) each frame is shown; default 2 |
| `interpolate` | `true` blends each frame smoothly into the next (slow lava, water); default `false` (flickering fire) |

The pattern is painted once per frame with the same seed and a phase from 0 to just below 1 (frame / frames); the patterns `fire`, `lava` and `water` move with it so the last frame leads into the first without a jump, other patterns look the same in every frame. Each frame takes a tile of the atlas. A texture over several blocks (`tiles`) cannot animate.

## Fallback order

For every texture, in this order:

1. the active packs, highest priority first; within one pack a PNG before a spec of the same name, and a `procedural` pack then paints the content's own definition (not PNG textures of mods);
2. the content itself: the game's 16×16 textures and the mods' textures;
3. the magenta placeholder for names nothing defines.

All textures are scaled to the highest resolution among the active packs: lower ones are enlarged pixel by pixel, so a 32× pack combined with a 64× pack keeps its look.

## Limits and checks

Everything is checked when the pack is read; a bad file is skipped with a message and its texture falls back, nothing stops the game:

| Check | Limit |
|-------|-------|
| Manifest size | 64 KiB |
| Texture files per pack | 2048 (folders are read up to twice that, 64 namespace folders; at most 100 problems are listed) |
| Size of one PNG / one spec | 4 MiB / 64 KiB |
| PNG | real PNG signature, readable, exactly the manifest's resolution (or a strip of frames) |
| Decoded images per pack | 192 MiB; textures above 256× are kept at 256× |
| Decoded images of all active packs | 384 MiB; packs beyond it are left out, the lowest priority first |
| Active packs | 16 |
| Atlas (all mip levels) | 256 MiB and the GPU's largest texture: otherwise the resolution is halved (a 512× pack is shown at 256×, the settings say so) |

The atlas sizes of the shipped packs with the base game (346 textures): 16× 380×380 pixels, 0.7 MiB; 32× 760×760, 2.9 MiB; 64× 1520×1520, 11.7 MiB; 128× 3040×3040, 47 MiB (all mip levels included).

## Tools

```sh
# all textures of the game at 32x as an image pack (procedural HD textures as the source)
qtcraft-headless --no-mods --export-textures ~/my_pack --texture-resolution 32 --texture-packs builtin:qtcraft_hd_32
# only a few textures, a name for the manifest; mods with --mods-dir
qtcraft-headless --export-textures ~/my_pack --texture-keys qtcraft:stone,qtcraft:dirt --texture-pack-name "My pack"
# size, memory and build time of the atlas with each built-in pack
qtcraft-headless --atlas-stats
```

`scripts/make-texture-packs.sh` makes the built-in packs' previews and the Storybook pack (export plus `scripts/texturepack-style.py`); `scripts/texturepack-shots.sh` takes the comparison screenshots `docs/screenshots/28-*.png`.

## Mods

A mod's textures (`textures/*.json` or `*.png` in the mod) are part of its content and reach every player through the server. A texture pack overrides them like the game's: `textures/<mod id>/<name>.png`. Procedural mod textures get the HD look from QtCraft HD automatically. A mod may in addition ship texture packs in `<mod>/texturepacks/<folder>/`; players who have the mod installed find them in the settings (the example mod's `rubies_hd`).
