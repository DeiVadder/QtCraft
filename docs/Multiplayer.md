# Multiplayer

QtCraft lets several players share a world over TCP, in a LAN or over the internet, with a direct
connection to the server's address and port. There are no accounts, no relay or matchmaking
servers and no encryption. This document covers running a server, joining one, what the login
checks and, honestly, what it does not protect against. Decisions: E-251 to E-261, E-270, E-271 and
E-272 to E-277 in `docs/Entscheidungen.md`.

## Running a dedicated server

```sh
qtcraft-headless --server                       # settings from <data>/server.json
qtcraft-headless --server --port 25600 --max-players 8 --world Island --motd "Welcome"
QTCRAFT_SERVER_PASSWORD=secret qtcraft-headless --server --console
```

- `<data>` is the game's data folder (macOS: `~/Library/Application Support/QtCraft/qtcraft-headless`
  for the headless program, overridable with `--data-dir` or `QTCRAFT_DATA_DIR`).
- On the first start the server writes `<data>/server.json` with every setting (readable only by
  the user, it may hold the password). Command line options override the file for this run:

| Key in server.json | Option | Default | Meaning |
|---|---|---|---|
| `serverName` | – | QtCraft server | Shown to players at the login |
| `motd` | `--motd` | empty | Message of the day, shown at the login |
| `port` | `--port` | 25600 | TCP port (0: any free one) |
| `bindAddress` | `--bind` | empty (all interfaces, IPv4 and IPv6) | e.g. `127.0.0.1` for this computer only |
| `maxPlayers` | `--max-players` | 8 | Players at most; operators may still join a full server |
| `password` | `--password`, `QTCRAFT_SERVER_PASSWORD` | empty (none) | Server password; the option shows in the process list, prefer the file or the variable |
| `world` | `--world` | World | Folder below `<data>/worlds`; created when missing |
| `seed`, `worldType` | `--seed`, `--generator` | random, terrain | For a new world only |
| `viewDistance` | `--view-distance` | 8 | Chunks streamed at most; clients may ask for fewer |
| `loginTimeoutSeconds` | – | 10 | A login must be done within this time |
| `connectionsPerSecond`, `connectionBurst`, `maxConnectionsPerAddress` | – | 1, 8, 8 | Limits for new connections per address |
| `operators` | – | empty | Operators by name (bound to their identity at their login) |

- Without `--console` the server reads no standard input and runs as a service until it is
  stopped. **SIGTERM** (service managers, `kill`) and **SIGINT** (Ctrl+C) save the world and every
  player, tell the players "The server stopped" and end the process with code 0; a second Ctrl+C
  ends it at once. With `--console`, server commands are typed on standard input (`help`, `stop`,
  `op`, `ban`, `backup`, …). On Windows, Ctrl+C and Ctrl+Break in the console stop the server the same
  way (untested); closing the console window, logging off or shutting Windows down ends the process
  before it has saved: the world keeps its last autosave and the players their last save. Stop the
  server with `stop` or Ctrl+C first.
- The server prints `Listening on port N` when it is ready.
- Operators, bans and the whitelist are kept per world in `access.json`, the players' identities
  in `identities.json` (both in the world folder; see "Identity" below). New identities are written
  with the next tick and then at most once a second, and always when the server saves or stops.
- Operator names in `server.json` must be valid player names as written (no `<`, `>`, `&`, not only
  punctuation); otherwise the server does not start and names the entry. Each of them is bound to the
  identity of his first login, kept in `access.json` (`configOperators`): after that only this
  installation has the rights under that name. To hand the name to another installation, remove its
  entry from `configOperators` while the server is stopped.

### From the app bundle (macOS release)

The release bundle carries the dedicated server next to the game, on the same Qt frameworks
(`QtCraft.app/Contents/MacOS/qtcraft-headless`, universal for Apple silicon and Intel). Start it from
Terminal; it needs no window and no other installation:

```sh
/Applications/QtCraft.app/Contents/MacOS/qtcraft-headless --server --port 25600
/Applications/QtCraft.app/Contents/MacOS/qtcraft-headless --server --console --data-dir ~/qtcraft-server
```

- Without `--data-dir` the server keeps its worlds in `~/Library/Application Support/QtCraft/qtcraft-headless`,
  apart from the game's own worlds (`~/Library/Application Support/QtCraft/QtCraft`). To serve a
  world made in the game, pass `--data-dir "$HOME/Library/Application Support/QtCraft/QtCraft" --world <folder>`
  while the game does not have that world open (the world lock refuses a second user).
- The bundle is signed ad hoc only: if macOS refuses to start the game the first time (see the
  README), it refuses the server too; once the game has been allowed, the server runs as well.
- `scripts/release.sh` checks that the server of the bundle starts (`--server --port 0 --bind
  127.0.0.1`, temporary data folder), prints `Listening on port N` and stops with exit code 0 on
  SIGTERM.

## Hosting from the game

A single-player world can be opened to other players while its owner keeps playing: the local
player stays in-process, a listener in the server thread accepts network players
(`game.multiplayer.openToLan(port, password, maxPlayers)`; the command line test hook is
`qtcraft --play --host-lan <port>`). The owner is always an operator.

## Joining

The game connects to `host`, `host:port`, `1.2.3.4:port` or `[ipv6]:port` (the port defaults to
25600) with a player name and, if the server has one, its password
(`game.multiplayer.join(address, name, password)`; command line: `qtcraft --join host:port
--player-name Name --password …`). A refusal comes back with a translated reason.

## In the game (user interface)

- **Main menu → Multiplayer** (E-258): server address, player name, password (optional). While
  the game connects and logs in the screen shows the step and a Cancel button; a refusal or a
  connection error stays on the screen, translated (the server's reason key, or a socket error
  such as "Nobody accepts connections at this address and port"). **Recent servers**: up to ten
  servers joined successfully, newest first, with the name the server gave and the time of the
  last join; a click takes the address, Join joins, × removes. The list and the player name live
  in `settings.ini` (group `multiplayer`). **Passwords are never stored**; the last one is kept in
  memory only, for "Join again".
- **Pause menu → Open to LAN** (E-260, single-player worlds only): port (0 for any free one),
  optional password, players at most. Once open the screen shows the port and up to four
  addresses of this computer (`address:port`, IPv4 first, no loopback, link-local or temporary
  IPv6 addresses) for friends to type, a hint about the firewall and port forwarding, and **Stop
  hosting**, which closes the listener and disconnects the network players ("The host stopped
  sharing the world"; they are saved). The pause menu button then reads "Open on port N". Leaving the
  world closes the listener; network players are disconnected with "The server stopped".
- **Open to LAN listens on every interface**, IPv4 and IPv6, not only the local network: on a network
  with IPv6 and no firewall rule against incoming connections (many home routers block them, not all;
  a laptop on a public network may have none) the world is reachable from the internet at the
  computer's IPv6 address. Set a password when you open a world on such a network.
- **Player list** (E-259): hold **Tab** (key action `playerList`, rebindable under Settings →
  Controls). Shows the server's name (the world's name for an own world), its message of the day,
  the number of players and every player sorted by name with an "Operator" mark; the own entry is
  highlighted. There is no ping: the protocol does not measure round trips.
- **Names and texts from the network are shown as plain text** (chat, player names, the server's name
  and message of the day, reasons, labels in the world): a name like `<img src=…>` is shown as typed
  and never loads anything. The server refuses player names with `<`, `>` or `&` ("Player names may
  not contain <, > or &"), the join form already before connecting. Names of blocks and items come
  from the server's content and are shown like the game's own (the server's operator chose them).
- **Join and leave lines**: the chat shows "X joined the game" / "X left the game", made by each
  client from the player list it receives (no server message).
- **Disconnect screen** (E-261): when the session ends without the player leaving (server stopped,
  kicked, timed out, connection lost) the game shows "Disconnected from the server" with the
  reason, **Join again** and **Back to menu**. Loading a network world can be cancelled; in a
  network game the pause menu's last button is **Disconnect**.
- **Test hooks**: `--overlay multiplayer` (main menu; with `--join` the join runs in the screen and
  a refusal stays visible), `--overlay lan`, `--overlay players`, and `QTCRAFT_LAN_BIND=127.0.0.1`
  to open a world on this computer only (it then lists only that address). Screenshots:
  `docs/screenshots/25-multiplayer-menu.png`, `25-join-refused-de.png`, `25-open-to-lan.png`,
  `25-player-list.png`, `25-disconnected-de.png`.
- **Known limits:** the host of an opened world plays as "Player" (the name of single-player
  worlds, whose player file hangs on it); no server discovery in the LAN, the host reads out an
  address. The content compared at a join is read once and kept until the mods are switched or
  looked for anew (Mods screen) or a world was played: a mod changed on disk while the game runs
  needs that to count.

## Ports, firewall and NAT

- The server listens on one TCP port (25600 by default). In a LAN, allow incoming connections on
  that port in the server computer's firewall (macOS asks once when the server first listens).
- Over the internet the server must be reachable: forward the TCP port in the router to the
  server computer (port forwarding), or run it on a machine with a public address. QtCraft does no
  NAT traversal (no UPnP, no hole punching) and uses no relay servers.
- IPv6 works where both sides have it; a server bound to all interfaces accepts IPv4 and IPv6. With
  IPv6 there is no NAT: unless a firewall blocks it, a server (or a world opened to the LAN) on all
  interfaces is reachable from the internet at its IPv6 address. Bind to an IPv4 address
  (`bindAddress`, e.g. `192.168.1.20`) to stay within the LAN.

## What the login checks

1. **Version:** client and server must speak the same protocol version.
2. **Password:** never sent in plain text. Both sides derive a key with PBKDF2-HMAC-SHA256 (20 000
   rounds) from the password and the world's salt; the client proves it knows the key with an HMAC
   of a fresh random nonce.
3. **Content:** the server and the client compare their content packs (base game and mods): id,
   version and a hash over the files the game loads from the pack (`pack.json`, the content folders'
   JSON files, `textures/*.png`, `worldgen/*.json`; hidden files such as `.DS_Store` or `._name.json`
   and other files such as notes do not count). Any difference is refused with the differences named;
   a client that sends no hash for a pack the server hashed counts as different. Packs the server
   could not hash (larger than 256 MB or more than 20 000 files) are compared by id and version.
4. **Identity:** every installation has a random 32-byte key (`<data>/player.key`, readable only by
   the user). For each server the game derives an identity from it (HMAC with the server's salt);
   the key itself never leaves the computer and no two servers see the same identity. The first
   login of a name registers its identity (**trust on first use**); every later login of that name
   must prove the same identity with an HMAC of the nonce. Somebody else's installation is refused
   with "The name … belongs to another player on this server". When the store holds its maximum of
   65 536 names, new names are refused ("This server cannot register new players").
5. **Access:** bans and the whitelist (by name and identity fingerprint), the player limit, and no
   second player with the same name (ignoring case) online.

Operators, bans and whitelist entries carry the fingerprint of the player's identity: an operator
entry counts only for that identity, and a ban with a fingerprint also holds under other names of
the same installation.

## Limits of this security (read before opening a server to the internet)

QtCraft is a hobby project; its network security guards against mistakes and simple abuse, not
against a determined attacker.

- **No encryption.** Everything, chat included, travels in plain text. Anybody on the path
  (the same Wi-Fi, a provider) can read it and could take over a running connection.
- **No server authentication.** The client cannot tell whether it talks to the real server.
- **The first login sends the identity for that server in plain text.** Whoever records it can log
  in as that player on that server. Later logins send only proofs bound to a fresh nonce, which
  cannot be replayed. But the client sends the identity whenever the challenge says the name is not
  known yet: **a hostile server, or anybody who can change the traffic on the way (man in the middle),
  can claim that on any login** and so receive the identity of every player who joins, not only on
  their first login.
- **Passwords can be guessed offline** by whoever records a login: PBKDF2 slows guessing down, a
  long password is still needed.
- **A hostile server** could present the salt of another server and so learn the player's identity
  for that other server (only for players who join it).
- **The key is the player.** Whoever copies `player.key` can play as that player everywhere; whoever
  loses it starts as a new player (a server then refuses the old name until an operator helps,
  e.g. by removing the name from `identities.json`). Deleting the key also escapes bans by
  fingerprint.
- **Trust on first use:** a name nobody has used on a server yet belongs to whoever uses it first.
  Operators named in advance (`/op Name`, `operators` in `server.json`) are bound at that first
  login.
- Limits on messages, frame sizes, connections per address and the login time protect the server
  from floods and broken clients, not from many machines at once. The limits per address count an
  IPv6 client by its /64 network (a host picks the rest of its address freely), an IPv4 client by
  its address (a whole household behind one NAT shares the limits).

## Network behaviour

- Frames are length-prefixed; a keep-alive goes out after 5 s without traffic, a connection without
  traffic for 30 s counts as lost: the player is removed and saved.
- A client that cannot keep up gets new chunks only while less than 4 MiB wait to be sent, and is
  cut off beyond 64 MiB: the server logs "The connection is too slow", the player only sees the
  connection lost (a reason could not get past the 64 MiB waiting before it).
- The own player is predicted by the client and corrected by the server; with round trips of 50 to
  300 ms walking, jumping and turning needed no correction in the tests (E-257). Mining shows the
  break once the server confirms it (one round trip), placing likewise.

## Other players and creatures on screen

The server sends where every player and creature in view is once per tick (20 times a second),
stamped with the server's time of that tick. Over a real network these messages arrive unevenly:
one late, then two at once. Showing each as it arrives makes the others stop and jump. The client
therefore shows them a little in the past (E-271):

- It remembers the recent positions of each entity with their server time and shows the moment
  **100 ms** before the newest message could have arrived (the fastest arrival of the last two
  seconds sets that estimate), interpolating between the two positions around it. A message that is
  up to about 50 ms later than the fastest one is already there when it is needed.
- Later messages are bridged: the movement goes on in its direction for at most 100 ms, then the
  entity waits. A jump of more than 10 m (a teleport, a respawn) shows at once.
- At another tick rate (`/tick rate`) the delay grows to one interval plus 50 ms (1050 ms at one
  tick per second). The shown time does not jump while it adapts to a changed connection: it runs at
  most 10 % faster or slower. A change of the tick rate by a factor of two or more is taken at once
  (one jump instead of ten seconds of stopping or rushing figures), once the clock has seen it: at
  once when the rate goes up, after up to two seconds when it goes down.
- Health, swings, hurt flashes, sneaking and the held item show when they arrive, up to 100 ms
  before the matching movement; the own player is not delayed (prediction, see above).
- Measured (`test_interpolation`): a walker reported every tick and received 50 to 150 ms late
  keeps its speed on screen within 1 to 2 % and never stops or goes backwards; showing each
  message on arrival stops it for up to 48 ms and moves it at 0.5 to 1.5 times its speed. With
  messages up to 200 ms late (beyond the allowance) the buffer stays within 10 % (newest message:
  stops of 96 ms, up to twice the speed). Two real sessions on one server with 40 to 100 ms of
  one-way delay (four runs): the walker moves on screen at 0.75 to 1.2 times its speed without
  stops, versus 0.15 to 1.7 times with stops of 54 to 96 ms (the remaining unevenness comes from the
  server, which moves a player per input: now and then two inputs fall into one of its ticks).

## Bots and multiplayer tests

`qtcraft-headless --join host:port --player-name Name` connects the control-interface bot of the
headless program to a server instead of starting a world in the same process (E-270, commands in
`docs/Steuerschnittstelle.md`). Each bot needs its own data folder (`--data-dir`), which holds its
`player.key`: a second bot with the same name and another key is refused like a stranger. Exit
codes: 3 when the join fails, 4 when the server ends the connection.

```sh
qtcraft-headless --server --console --data-dir /tmp/srv --bind 127.0.0.1 --port 0 --generator flat
printf '%s\n' '{"cmd":"waitUntilReady"}' '{"cmd":"chat","text":"hello"}' '{"cmd":"players"}' \
  | qtcraft-headless --join 127.0.0.1:<port> --player-name Alice --data-dir /tmp/alice
```

`test_multiplayer_bots` starts a dedicated server and four bots as processes of their own on
127.0.0.1 (random free port) and checks through the control interface: everyone sees everyone
(player list and entities), an operator made on the server console can use `/time` while another
player is refused, the time of day agrees on all clients, look, armour and animation (sneaking,
walking speed) reach the others, chat reaches everyone, a block mined and placed by hand changes
for all, water flows and a torch lights its surroundings on every client, a player who leaves
disappears for the others and is saved, rejoining restores position and inventory, a second
installation cannot take a name in use, and stopping the server ends the bots with code 4. It polls
for each condition (60 s at most) instead of waiting fixed times; a run takes about 16 s.

## Backups

`/backup [label]` (operators, console) saves the world, freezes its files with hard links in a
moment and copies them in the background; the game goes on meanwhile and the backup holds the world
as it was at the command. The backup folder appears under `<data>/backups` when the copy is
complete.
