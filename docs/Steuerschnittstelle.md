# Control interface

The control interface lets an external agent or a test script play QtCraft like a human player and observe the game in structured form. It is a client of the normal protocol: commands drive a `QcClientSession`, which talks to the server only through protocol messages. There is no shortcut around the server.

## Transport

Newline-delimited JSON (one request object per line, one response object per line).

| Mode | How to start | Notes |
|------|--------------|-------|
| Standard input/output | `qtcraft-headless` | Requests on stdin, responses on stdout, logs on stderr. EOF or `quit` ends the process. |
| Script | `qtcraft-headless --script file.jsonl` | Runs all requests in order and exits with code 0 if every request succeeded, 1 otherwise, 2 on time limit. Lines starting with `#` are comments. |

The interface is **off by default**: the graphical game does not open it. It only exists in processes started explicitly with the options above and never listens on a network port. A local socket in the graphical game (e.g. `--control local:<name>`) is not implemented; it would only forward the same lines.

`qtcraft-headless` options: `--seed <n>`, `--world <name>`, `--view-distance <chunks>` (default 4), `--time-limit <seconds>` (default 120), `--script <file>`, `--data-dir <dir>` (data directory like `QTCRAFT_DATA_DIR`: `worldCreate`/`worldLoad` keep worlds in `<dir>/worlds`, mods come from `<dir>/mods`; default: the game's own data directory), `--mods-dir <dir>` (overrides the mods directory), `--no-mods`, `--export-sounds <dir>` (writes every sound and the music piece of `--seed` as WAV files and exits), `--export-handbook <dir>` with optional `--language <locale>` (writes the handbook of the base game, with `--mods-dir` also of those mods, as Markdown and exits; problems go to stderr). Without a world command the bot plays a throw-away world that is not saved.

## Requests and responses

Every request is an object with `cmd` and an optional `id`, which is echoed in the response. Parameters are type-checked (numbers, booleans); a line that is not a JSON object is answered with an error mentioning its line number, in order with the other responses. Successful responses contain `"ok": true`; failures contain `"ok": false` and `"error"`. Commands that take time (`move`, `jump`, `wait`, `waitUntilReady`) answer when they complete and report the player status at that moment. Requests are processed strictly one after another.

### Player status

Returned by `status` and by all tick-based commands:

```json
{"ok":true,"state":"Playing","tick":63,"world":"Headless","seed":"0",
 "position":[1.7,68,0.5],"velocity":[0,0,0],"onGround":true,"inWater":false,
 "yaw":90,"pitch":0,"loadedChunks":81}
```

Additional fields: `health`, `maxHealth`, `dead`, `food`, `maxFood`, `worldTime` (seconds; a day lasts 600 s), `night`, `targetEntity` (id of the creature under the crosshair), `target` (the block the player looks at within 5 m: `x`, `y`, `z`, `face`, `block`), `selectedSlot`, `held` (`{"item","count"}` or `null`) and `messages` (chat lines received while the request ran).

`position` is the centre of the player's feet in metres (1 block = 1 m). `yaw` 0 looks north (-Z), 90 east (+X), 180 south (+Z); `pitch` is positive upwards. `state` is one of `Connecting`, `Loading`, `Playing`, `Disconnected` (then `disconnectReason` is set). `tick` counts client simulation ticks (20 per second).

### Commands

| Command | Parameters | Effect |
|---------|-----------|--------|
| `status` | – | Player status. |
| `waitUntilReady` | `timeoutPolls` (50 ms each, default 1200) | Waits until the session plays, the 3×3 chunks around the player are loaded and the player stands or swims. |
| `look` | `yaw`, `pitch` (degrees) | Sets the view direction. |
| `move` | `forward`, `strafe` (-1..1), `jump`, `sneak`, `sprint` (bool), `ticks` (default 20) | Applies the movement intent for the given number of ticks, then stops. |
| `jump` | `ticks` (default 2) | Jumps (same as `move` with `jump: true`). |
| `wait` | `ticks` (default 20) | Lets time pass. |
| `block` | `x`, `y`, `z` | Name of the block at a position (`null` if not loaded), its `state` (property → value, e.g. `{"facing":"east","half":"bottom"}`, empty for blocks without states) and `blockLight` (0–15, computed from the light sources around it). |
| `blocks` | `radius` (0..8, default 3) | All non-air blocks in a cube around the player's feet: `[{"x":..,"y":..,"z":..,"block":"qtcraft:stone"}]`; blocks with states also carry `state`. |
| `lookAt` | `x`, `y`, `z` | Turns towards the centre of a block. |
| `mine` | optional `x`, `y`, `z`; `maxTicks` (default 400) | Looks at the block (or keeps the current target) and mines until it is gone. The server checks the mining time; drops go into the inventory. |
| `place` / `use` (also eats held food when hungry, opens crafting tables, furnaces and chests, opens and closes doors, trapdoors and gates; an empty bucket aims at liquids and takes a source, a full one pours it; a clicked tall grass is replaced) | optional `x`, `y`, `z`, `face` (`east`, `west`, `up`, `down`, `south`, `north`, default `up`) | Looks at the centre of that face and uses the held item on it, which places a block next to it; the server derives its state (facing from the view, upper or lower half from the face, attachment towards the clicked block). Answers after 4 ticks; check the block and its state with `block`. |
| `select` | `slot` (0–8) | Selects a hotbar slot; answers with the inventory. |
| `inventory` | – | `{"selectedSlot":0,"slots":[{"slot":0,"item":"qtcraft:dirt","count":3}],"craftingSize":2,"craftingGrid":[...],"craftingResult":null}` |
| `moveItem` | `from`, `to` (0–35 inventory, 36–44 crafting grid, 3 per row; 2 × 2 uses 36, 37, 39, 40; 45–47 the open machine's input, fuel and output, or 45 onwards the slots of an open chest), optional `count` (0 = whole stack) | Moves, splits or merges stacks. The machine's fuel slot takes only fuel, its output nothing. |
| `openMachine` | `x`, `y`, `z` | Looks at the block and uses it; answers with the machine state once the server opened it, fails when the block is out of reach, hidden or no machine. Works for chests too. |
| `machine` | – | State of the open machine: `{"open":true,"position":[x,y,z],"category":"smelting","input":{"item","count"},"fuel":…,"output":…,"burn":0.8,"progress":0.25}` (`burn`: share of the current fuel left, `progress`: share of the current item done; empty slots are `null`), or `{"open":false}`. For a chest: `{"open":true,"position":[x,y,z],"chest":true,"slots":[stack or null, …]}`. |
| `fillMachine` | `item`, optional `into` (`input` default, or `fuel`), `count` (0 = whole stack) | Moves the first inventory stack of that item into the open machine; answers with the machine state. |
| `takeOutput` | optional `count` | Moves the output into a matching stack with room or an empty inventory slot; answers with the machine state. |
| `closeMachine` | – | Closes the machine screen (the server also closes it when the player walks away, dies or the block is broken). |
| `takeResult` | – | Takes the crafting grid's result once into the inventory. |
| `closeCrafting` | – | Puts the grid's items back into the inventory and leaves a crafting table's 3 × 3 grid. Items in the grid are also kept when the player disconnects or the world is saved. |
| `recipes` | optional `all` (bool) | Recipes the inventory can make now (or all): `recipe`, `result`, `count`, `needs`, `needsCraftingTable`, `possible`. |
| `craft` | `recipe`, optional `count` (1–64) | Crafts directly from the inventory (recipe book). 3 × 3 recipes need a crafting table within 5 m. The result appears in `messages` and the inventory. |
| `sounds` | optional `clear` (bool) | The last 64 sound cues (oldest first), each `{"key","origin":"server"\|"local","position","volume","pitch","tick","known","distance","gain","pan"}` (`gain` and `pan` as heard from where the player is now; `distance`, `gain`, `pan` only for sounds the content defines), and `"ambience":{"wind","cave","water"}` (current loop levels 0–1). With `clear` the list is emptied after answering. The headless bot plays no audio; scenario tests use this to check sounds. |
| `mods` | – | Content packs the server loaded, base game first: `{"packs":[{"id","name","version"}]}`. |
| `handbook` | optional `language` (default `en`; only languages the content has, others fail), and one of `entry` (an entry id such as `block:qtcraft:iron_ore` or a content name such as `iron_ore`, `qtcraft:zombie`), `search` (text), `category` (`playing`, `blocks`, `parts`, `liquids`, `items`, `tools`, `food`, `creatures`, `biomes`, `worldgen`, `tags`, `tables`) | The handbook built from the loaded content (only in `qtcraft-headless`). Without parameters: `{"categories":[{"id","title","count"}],"entries":n,"problems":[…]}`. `entry`: `{"entry":{"id","name","category","categoryTitle","pack","packName","title","icon","text","facts":[{"label","value","links"}],"sections":[{"title","text","links","recipes"}]}}` with links `{"target","text","icon","note"}` and recipes `{"kind":"crafting"\|"machine","name","shapeless","needsTable","width","height","cells","result","count","seconds","machines","note"}` (empty cells are `null`); `search`: `{"results":[{"id","title","icon","category","pack"}]}` (best first, titles in the language and in English); `category`: `{"entries":[…]}`. Unknown entries or categories fail. |
| `chat` | `text` | Sends a chat line or a command; server replies appear in `messages`. Commands: `/help`, `/give <item> [count]`, `/save`, `/time set <day\|night\|seconds>`, `/time add <seconds>` (time skip), `/summon <mob> [count]` (at most 64 creatures in total), `/kill`, `/heal`, `/feed [0..20]` (sets the food points), `/gamerule keepInventory [true\|false]` (without a value: show it), `/spawnitem <item> [count]` (drops up to 64 items two metres ahead), `/setblock <x> <y> <z> <block>` (`~` or `~n` for coordinates relative to the player's feet; saplings start growing, `air` breaks the block without drops; the block may carry a state, `oak_stairs[facing=east,half=top]`, otherwise connections follow the neighbours), `/fill <x1> <y1> <z1> <x2> <y2> <z2> <block>` (the same for a box of at most 32768 blocks; water, lava and sand set this way flow and fall), `/locatebiome <biome>` (nearest column of that biome, e.g. `swamp`). |
| `worldList` | – | `{"worlds":[{"folder","name","seed","error"}]}` |
| `worldCreate` | `name`, optional `seed` (integer; random when omitted) | Creates a saved world and switches to it; answers with its `folder`. Follow with `waitUntilReady`. |
| `worldLoad` | `folder` | Switches to a saved world (the current one is saved first). Follow with `waitUntilReady`. |
| `worldSave` | – | Saves the current world and reports failures (`ok: false` with the reason). |
| `entities` | `radius` (1–64, default 32) | `{"entities":[{"id","type","name","hostile","position","distance","health"}]}` – mobs and other players in view. Dropped items (sent within 32 m) appear as `{"id","type":"item","item","count","position","distance"}`, falling blocks as `{"id","type":"fallingBlock","block","position","distance"}`. |
| `view` | optional `mode` (`first`, `behind`, `front`) | Sets the view mode of the graphical client (the headless bot only stores it) and answers with the current `mode`. |
| `drop` | optional `all` (bool) | Throws one item (or the whole stack) of the selected hotbar slot ahead; it can be picked up again after 2 s. Fails when the slot is empty. |
| `collect` | optional `radius` (1–32, default 8), `maxTicks` (default 200) | Walks to the nearest dropped item within the radius again and again until none is left or the time is up; items are picked up by touching them. |
| `attack` | `entity` (id from `entities`) | Turns towards the entity and hits it with the held item (reach 4 m). Fails if the entity is out of reach, hidden behind blocks, or the hit did not land (0.4 s attack cooldown, short invulnerability after a hit). |
| `useEntity` | `entity` (id from `entities`) | Turns towards the creature and uses the held item on it (an empty glass bottle catches a firefly). Fails if the creature is out of reach or hidden, or when nothing happened (the held item does not catch it). |
| `respawn` | – | Leaves the death screen; answers when the player is alive again. |
| `quit` | – | Ends the process (the current world is saved). |

All tick-based commands fail with `"Timed out"` if the session stops ticking for much longer than requested (e.g. after a disconnect).

Not available: screenshots through the interface (the graphical game's `QTCRAFT_SCREENSHOT` hook covers visual checks) and a local socket in the graphical game (see above).

## Example

```sh
printf '%s\n' '{"cmd":"waitUntilReady"}' '{"cmd":"look","yaw":90}' \
              '{"cmd":"move","forward":1,"ticks":40}' '{"cmd":"status"}' '{"cmd":"quit"}' \
  | build/debug/src/headless/qtcraft-headless --seed 42
```

The scenario tests (`tests/test_scenarios.cpp`, `tests/scenarios/*.jsonl`) use exactly this interface.

## Relation to future bridges

The interface is plain JSON over a byte stream, so an MCP server or any other bridge can be layered on top by forwarding lines; no protocol or game code would change.
