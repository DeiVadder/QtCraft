# Control interface

The control interface lets an external agent or a test script play QtCraft like a human player and observe the game in structured form. It is a client of the normal protocol: commands drive a `QcClientSession`, which talks to the server only through protocol messages. There is no shortcut around the server.

## Transport

Newline-delimited JSON (one request object per line, one response object per line).

| Mode | How to start | Notes |
|------|--------------|-------|
| Standard input/output | `qtcraft-headless` | Requests on stdin, responses on stdout, logs on stderr. EOF or `quit` ends the process. |
| Script | `qtcraft-headless --script file.jsonl` | Runs all requests in order and exits with code 0 if every request succeeded, 1 otherwise, 2 on time limit. Lines starting with `#` are comments. |

The interface is **off by default**: the graphical game does not open it. It only exists in processes started explicitly with the options above and never listens on a network port. (*Planned:* `--control local:<name>` for a local socket in the graphical game.)

`qtcraft-headless` options: `--seed <n>`, `--world <name>`, `--view-distance <chunks>` (default 4), `--time-limit <seconds>` (default 120), `--script <file>`, `--data-dir <dir>` (where `worldCreate`/`worldLoad` keep worlds; default: the game's own world directory). Without a world command the bot plays a throw-away world that is not saved.

## Requests and responses

Every request is an object with `cmd` and an optional `id`, which is echoed in the response. Parameters are type-checked (numbers, booleans); a line that is not a JSON object is answered with an error mentioning its line number, in order with the other responses. Successful responses contain `"ok": true`; failures contain `"ok": false` and `"error"`. Commands that take time (`move`, `jump`, `wait`, `waitUntilReady`) answer when they complete and report the player status at that moment. Requests are processed strictly one after another.

### Player status

Returned by `status` and by all tick-based commands:

```json
{"ok":true,"state":"Playing","tick":63,"world":"Headless","seed":"0",
 "position":[1.7,68,0.5],"velocity":[0,0,0],"onGround":true,"inWater":false,
 "yaw":90,"pitch":0,"loadedChunks":81}
```

Additional fields: `target` (the block the player looks at within 5 m: `x`, `y`, `z`, `face`, `block`), `selectedSlot`, `held` (`{"item","count"}` or `null`) and `messages` (chat lines received while the request ran).

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
| `block` | `x`, `y`, `z` | Name of the block at a position, `null` if not loaded. |
| `blocks` | `radius` (0..8, default 3) | All non-air blocks in a cube around the player's feet: `[{"x":..,"y":..,"z":..,"block":"qtcraft:stone"}]`. |
| `lookAt` | `x`, `y`, `z` | Turns towards the centre of a block. |
| `mine` | optional `x`, `y`, `z`; `maxTicks` (default 400) | Looks at the block (or keeps the current target) and mines until it is gone. The server checks the mining time; drops go into the inventory. |
| `place` / `use` | optional `x`, `y`, `z`, `face` (`east`, `west`, `up`, `down`, `south`, `north`, default `up`) | Looks at that face and uses the held item on it, which places a block next to it. Answers after 4 ticks; check the block with `block`. |
| `select` | `slot` (0–8) | Selects a hotbar slot; answers with the inventory. |
| `inventory` | – | `{"selectedSlot":0,"slots":[{"slot":0,"item":"qtcraft:dirt","count":3}]}` |
| `moveItem` | `from`, `to` (0–35) | Moves or merges an inventory stack. |
| `chat` | `text` | Sends a chat line or a command (`/help`, `/give <item> [count]`); server replies appear in `messages`. |
| `worldList` | – | `{"worlds":[{"folder","name","seed","error"}]}` |
| `worldCreate` | `name`, optional `seed` (integer; random when omitted) | Creates a saved world and switches to it; answers with its `folder`. Follow with `waitUntilReady`. |
| `worldLoad` | `folder` | Switches to a saved world (the current one is saved first). Follow with `waitUntilReady`. |
| `worldSave` | – | Saves the current world and reports failures (`ok: false` with the reason). |
| `quit` | – | Ends the process (the current world is saved). |

All tick-based commands fail with `"Timed out"` if the session stops ticking for much longer than requested (e.g. after a disconnect).

*Planned with the corresponding stages:* break/place blocks, inventory, crafting, chat and commands, world create/load/save, time skip, entity observation, screenshots.

## Example

```sh
printf '%s\n' '{"cmd":"waitUntilReady"}' '{"cmd":"look","yaw":90}' \
              '{"cmd":"move","forward":1,"ticks":40}' '{"cmd":"status"}' '{"cmd":"quit"}' \
  | build/debug/src/headless/qtcraft-headless --seed 42
```

The scenario tests (`tests/test_scenarios.cpp`, `tests/scenarios/*.jsonl`) use exactly this interface.

## Relation to future bridges

The interface is plain JSON over a byte stream, so an MCP server or any other bridge can be layered on top by forwarding lines; no protocol or game code would change.
