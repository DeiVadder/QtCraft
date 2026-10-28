# Multiplayer

QtCraft lets several players share a world over TCP, in a LAN or over the internet, with a direct
connection to the server's address and port, encrypted with TLS. There are no accounts and no relay
or matchmaking servers. This document covers running a server, joining one, the encryption, what
the login checks and, honestly, what it does not protect against. Decisions: E-251 to E-261, E-270,
E-271, E-272 to E-277 and E-872 in `docs/Entscheidungen.md`.

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
| `worldOptions` | `--world-options` | empty (the defaults) | World generation options of a new terrain world (E-917), e.g. `preset=more_villages,caves=off`; for a new world only, the world keeps them |
| `viewDistance` | `--view-distance` | 8 | Chunks streamed at most; clients may ask for fewer |
| `loginTimeoutSeconds` | – | 10 | A login must be done within this time |
| `pauseWhenEmptySeconds` | – | 60 | Seconds without anyone connected before the server pauses (0: never; E-877) |
| `connectionsPerSecond`, `connectionBurst`, `maxConnectionsPerAddress` | – | 1, 8, 8 | Limits for new connections per address |
| `operators` | – | empty | Operators by name (bound to their identity at their login) |
| `defaultGameMode` | `--game-mode` | empty (the world's own default) | `survival`, `creative` or `adventure`: the mode of players who join for the first time; a new world is created with it as its default (E-282, E-735) |
| `limits` | – | the defaults below | Budgets of the simulation (E-349), an object with the keys below |

**Limits** (`limits` in `server.json`; in a single-player world the object `limits` in its `world.json`, which a dedicated server ignores). A value out of its range is clamped and an unknown key ignored, each with a warning in the log; `/limits` (operators) shows the active values and where they come from.

| Key | Default | Range | Bounds |
|---|---:|---|---|
| `signalBudget` | 8192 | 256 – 4194304 | Signal Dust work per tick: dust looked at, components evaluated, blocks pushed, bodies looked at (E-335) |
| `signalScheduleReservePercent` | 25 | 0 – 90 | Share of the signal budget that block changes leave for due scheduled ticks |
| `signalWorkPerChange` | 4 | 1 – 64 | Work one block change counts for when a step is split (a step takes at most the budget left divided by it) |
| `signalMinimumStep` | 64 | 1 – 65536 | Changes or scheduled ticks a step takes at least |
| `signalMaxPendingChanges` | 1048576 | 1024 – 16777216 | Block changes near circuits waiting at most; further ones are dropped with a warning |
| `hopperBudget` | 4096 | 1 – 1048576 | Signal Hoppers acting per tick; the rest follow in the next ticks, round-robin (E-344) |
| `hopperCooldownTicks` | 8 | 1 – 1200 | Ticks a hopper rests after moving an item |
| `summonLimit` | 64 | 0 – 4096 | Awake creatures summoned by `/summon` at most; natural ones (animals of the chunks, night spawns, bred young) do not count; 0 switches summoning creatures off, armour stands stay possible (E-351) |
| `maxContainers` | 16384 | 16 – 1048576 | Containers (chests, barrels, hoppers, dispensers, droppers) in the world; placing one beyond is refused (E-357) |
| `maxMachines` | 4096 | 16 – 262144 | Machines (furnaces) in the world; placing one beyond is refused |
| `maxSaplings` | 4096 | 16 – 262144 | Growing saplings in the world; a sapling beyond does not grow |
| `blockUpdatesPerTick` | 1024 | 64 – 1048576 | Neighbour updates (connections, lost support, starting falls) per tick; those of player actions go first |
| `waitingBlockUpdates` | 65536 | 1024 – 16777216 | Neighbour updates waiting for unloaded chunks |
| `commandBlockUpdates` | 1048576 | 1024 – 16777216 | Neighbour updates queued by commands (`/fill`); further ones are dropped with a warning |
| `savedBlockUpdates` | 4194304 | 1024 – 4194304 | Pending neighbour updates written to a save (those of player actions first) |
| `liquidStepsPerTick` | 512 | 16 – 1048576 | Flow steps of liquids per tick (E-054) |
| `liquidPlannedSteps` | 262144 | 1024 – 16777216 | Flow steps of liquids planned at most; further cells flow again when touched |
| `fireStepsPerTick` | 256 | 16 – 1048576 | Steps of burning fires per tick, each a fire's chance to spread; the rest waits for the next tick (E-392) |
| `firesPerChunk` | 256 | 1 – 65536 | Burning fires in one chunk; beyond, no new fire is lit there and fire set by commands goes out |
| `fires` | 4096 | 16 – 1048576 | Burning fires in the world, the same way |
| `fireSpreadRadius` | 12 | 0 – 64 | Blocks fire spreads from the spark it began with along each axis; 0 switches spreading off on the whole server, whatever the game rule `fireSpread` of a world says |
| `fallingBlocks` | 2048 | 16 – 65536 | Falling blocks at once; beyond, blocks land straight or wait (E-055, E-288) |
| `itemsAwake` | 512 | 16 – 65536 | Dropped items awake in loaded chunks; new drops wait, surplus ones sleep |
| `itemsQueued` | 1024 | 16 – 1048576 | Drops waiting to appear; the oldest goes when full |
| `itemsTotal` | 4096 | 64 – 1048576 | Dropped items in the loaded world; the oldest goes when full (items of unloaded chunks are stored with them) |
| `monstersPer289Chunks` | 70 | 0 – 256 | Monsters spawning by themselves for every 289 loaded chunks within 8 chunks of the players, shared by players close together (decision E-836). It replaces `hostileCreaturesPerPlayer` (8 per player): files with the old key get a warning at the start and the default applies; rename the key to keep an own value |
| `passiveCreaturesPerPlayer` | 6 | 0 – 256 | Passive creatures spawning by themselves around one player |
| `ambientCreaturesPerPlayer` | 10 | 0 – 256 | Ambient creatures (fireflies) spawning around one player |
| `pathNodesPerTick` | 4096 | 0 – 1048576 | Nodes all path searches of creatures expand per tick; searches beyond wait for the next tick, first come first served; 0 switches path finding off and creatures walk straight at their goals (E-400) |
| `pathNodesPerSearch` | 512 | 16 – 65536 | Nodes one path search expands at most (a wandering creature a quarter); a goal out of reach gives a path to the closest cell reached (E-400) |
| `pathLength` | 64 | 8 – 256 | Waypoints a creature's path keeps; at the end of a cut path it searches on (E-400) |
| `crowdThrottled` | 8 | 2 – 4096 | Awake walking creatures in one block cell from which each thinks only every 4th to 10th tick (more often the fewer), without path searches, keeping its last goal and steering in between; fewer think fully (E-401) |
| `crowdIdle` | 24 | 2 – 4096 | Creatures in one block cell from which their AI is off: only gravity, currents, damage and the push of the crowd. In both crowd tiers a hostile creature still strikes a player in melee, so a knot of them is no safe place (E-401) |
| `breedingCrowd` | 24 | 2 – 1024 | Animals of a kind within 3 × 3 chunks above which no young are born (E-293) |
| `bredPerKind` | 256 | 0 – 65536 | Bred animals of a kind in the world at most |
| `randomTicksPerSection` | 3 | 0 – 64 | Random ticks per marked section and tick (growing plants, crops, soil, E-289); more makes plants grow faster, 0 stops them like the game rule `randomTicks` |
| `leafDecaysPerTick` | 8 | 1 – 4096 | Leaves decaying per tick after a trunk was removed |
| `leafChecksPerTick` | 64 | 1 – 65536 | Leaves checked per tick |
| `leafPendingChecks` | 16384 | 256 – 1048576 | Leaf checks waiting at most |
| `maxArmourStands` | 4096 | 16 – 65536 | Armour stands in the world (E-238) |
| `maxMinecarts` | 4096 | 16 – 65536 | Minecarts in the world (E-382) |
| `maxHangings` | 4096 | 16 – 65536 | Item frames and paintings in the world (E-790) |
| `experienceOrbs` | 1024 | 16 – 1048576 | Experience orbs in the world; beyond, dropped experience joins the nearest orb, so none is lost (E-398) |
| `movingBlocksShown` | 4096 | 16 – 65536 | Blocks shown sliding behind pistons at once; beyond, they appear at once (E-337) |
| `chunkGroupLimit` | 64 | 1 – 1024 | Chunks a group of connected circuits may span; a larger group loads chunk by chunk and its circuits wait at the edge of the loaded area (E-375) |
| `chunkUnloadsPerTick` | 32 | 1 – 4096 | Chunks unloaded per tick; a group is always unloaded whole (E-374) |
| `unsavedChunks` | 2048 | 16 – 1048576 | Unloaded chunks kept in memory (encoded) before a save in the background writes them (E-374) |
| `chunkTicketsPerTick` | 4 | 0 – 1024 | Chunks a signal may load per tick to reach parts of its line that are not loaded (E-376) |
| `chunkTickets` | 256 | 0 – 65536 | Chunks loaded for signals at once; a ticket counts every chunk of the group it loads (E-376) |
| `chunkTicketTicks` | 100 | 1 – 72000 | Ticks such a chunk stays loaded, counted from its arrival (E-376) |
| `forcedChunks` | 256 | 0 – 65536 | Chunks kept loaded by `/forceload`; each also keeps its circuit group (up to `chunkGroupLimit` chunks) (E-377) |
| `chunkLoadersPerPlayer` | 4 | 0 – 1024 | Chunk loader blocks a player may place (E-377) |
| `chunkLoaders` | 64 | 0 – 65536 | Chunk loader blocks that work in the world, the first placed first; each also keeps its circuit group (E-377) |
| `villages` | 4096 | 0 – 65536 | Villages the world keeps in `villages.json`; further ones are generated but not tracked (E-403) |
| `villageBeds` | 64 | 0 – 1024 | Beds listed per village (E-403) |
| `villageWorkstations` | 64 | 0 – 1024 | Workstations listed per village (E-403) |

Observers and dispensers have no bounds of their own: their pulses and shots are scheduled ticks inside the signal budget.

- Without `--console` the server reads no standard input and runs as a service until it is
  stopped. **SIGTERM** (service managers, `kill`) and **SIGINT** (Ctrl+C) save the world and every
  player, tell the players "The server stopped" and end the process with code 0; a second Ctrl+C
  ends it at once. With `--console`, server commands are typed on standard input (`help`, `stop`,
  `op`, `ban`, `backup`, …). On Windows, Ctrl+C and Ctrl+Break in the console stop the server the same
  way (untested); closing the console window, logging off or shutting Windows down ends the process
  before it has saved: the world keeps its last autosave and the players their last save. Stop the
  server with `stop` or Ctrl+C first.
- On the first start the server also makes its own TLS key and certificate, `<data>/server-key.pem`
  (readable only by the user) and `<data>/server-certificate.pem` (see "Encryption" below). It
  prints the certificate's fingerprint (`Certificate fingerprint (SHA-256): …`) at every start.
- The server prints `Listening on port N` when it is ready.
- **Pause when empty** (E-877): when no one has been connected for `pauseWhenEmptySeconds` (60 by
  default, counted in game time), the server saves like an autosave and stops ticking: the world,
  the time of day, creatures, plants, machines and circuits stand still and the tick counter stops.
  The log says `Server paused (no players)`, the `HEALTH` line says `paused` (its tick times then
  stay empty). The console, the admin channel and new connections keep working; the first
  connection resumes the server at once (`Server resumed`), before its login is done, so the login
  runs as usual. 0 keeps the server ticking without players.
- **Pause menu** (E-877): a world hosted in the game pauses while its own player has the pause menu
  open (also the settings and the handbook opened from there), but only while no one else is
  connected; a world opened to the LAN with a guest playing keeps running, and the pause menu says
  so. A dedicated server never pauses for one player's pause menu: the server ignores a network
  client's wish to pause.
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
- The bundle is signed ad hoc only: if macOS refuses to start the game the first time (see
  [Playing.md](Playing.md#starting-the-game)), it refuses the server too; once the game has been allowed, the server runs as well.
- `scripts/release.sh` checks that the server of the bundle starts (`--server --port 0 --bind
  127.0.0.1`, temporary data folder), prints `Listening on port N` and stops with exit code 0 on
  SIGTERM.

## Hosting from the game

A single-player world can be opened to other players while its owner keeps playing: the local
player stays in-process, a listener in the server thread accepts network players
(`game.multiplayer.openToLan(port, password, maxPlayers)`; the command line test hook is
`qtcraft --play --host-lan <port>`). The owner is always an operator. The world is offered over
IPv4 only unless the host switches on "Also over IPv6" (see below). The certificate is the
game's own (`server-certificate.pem` and `server-key.pem` in the game's data folder), made the
first time a world is opened.

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
  world closes the listener; network players are disconnected with "The server stopped". The open
  screen also shows the fingerprint of the host's certificate, for friends who want to compare it.
- **Open to LAN listens on every interface, over IPv4 only by default** (decision E-872): behind the
  router's NAT an IPv4 address stays in the local network unless a port is forwarded. **Also over
  IPv6** (a switch on the screen, off each time it opens) adds IPv6: on a network with IPv6 and no
  firewall rule against incoming connections (many home routers block them, not all; a laptop on a
  public network may have none) the world is then reachable from the internet at the computer's
  IPv6 address. Set a password when you switch it on.
- **A changed certificate** (decision E-872): when a server shows another certificate than at the
  first join to its address, the join is refused with both fingerprints and **Forget the old
  certificate** appears; only after that does the next join pin the new one. Use it only when the
  server's owner confirms that he made a new certificate.
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
- IPv6 works where both sides have it; a dedicated server bound to all interfaces (the default)
  accepts IPv4 and IPv6. With IPv6 there is no NAT: unless a firewall blocks it, such a server is
  reachable from the internet at its IPv6 address. Bind to an IPv4 address (`bindAddress`, e.g.
  `192.168.1.20`, or `0.0.0.0` for every IPv4 interface) to stay within the LAN. A world opened to
  the LAN uses IPv4 only unless the host switches IPv6 on.
- **Hosting over IPv6 only** (for example a home line with DS-Lite, which has no public IPv4
  address): start the dedicated server without `bindAddress` (one dual-stack socket for IPv4 and
  IPv6 on the port), open TCP 25600 for the server computer's IPv6 address in the router (often
  called "IPv6 exposed host" or a port release; the computer keeps its address only while its
  privacy extension does not change it, so prefer its stable address) and give players that
  address. They type it with or without brackets: `[2a02:…]:25600`, `[2a02:…]` or `2a02:…` alone
  (port 25600), the same on the command line (`--join`); a host name with an AAAA record works as
  well. The pin's key is the address in its canonical form, so `2A02:0DB8:0::1` and `2a02:db8::1`
  are one server. Tests: `test_tcp_transport::aDualStackListenerTakesIpv4Ipv6AndNames` (TLS
  handshake over 127.0.0.1, `[::1]` and `localhost` on one `Any` listener),
  `test_dedicated_server` (default bind `Any`), `test_login_protocol` (address forms),
  `test_network_client` (pin keys).

## Encryption (TLS)

Every network connection runs over TLS 1.3, and only over it (decision E-872, protocol 75): no
older protocol, no renegotiation, no cipher suites of TLS 1.2. The messages inside are the same as
before. The single-player game in the same process has no network
connection and is unaffected.

- **The server's certificate is its own.** At its first start a server (the dedicated server, or
  the game when it first opens a world to the LAN) makes an EC key on the curve P-256 and a
  certificate signed with it (ECDSA with SHA-256), valid until the year 9000, and keeps them in its data folder
  (`server-certificate.pem`, `server-key.pem`). No authority signs it. A damaged file, or one of the
  two alone, stops the start with a message and is never replaced: players have pinned the
  certificate, and a new one would look like an attack to them. Moving both files away makes a new
  certificate at the next start; every player then has to forget the old one.
- **Clients pin it at the first connection** (trust on first use, like SSH): the SHA-256
  fingerprint of the certificate goes into `<data>/known-servers.json` (readable only by the user)
  under the normalised address (`host:port`, a name in lower case, an IP address in its canonical
  form, so every spelling of one IPv6 address is one pin). Every later join to that address
  compares the certificate before anything is sent; another certificate refuses the join
  ("The server shows another certificate than at your first connection …") with both
  fingerprints. The same server under another name or address is pinned anew. Bots
  (`qtcraft-headless --join`) keep their pins in their own data folder. A program cannot join
  unchecked: without a file for the pins the join fails.
- **Comparing fingerprints:** the dedicated server prints its fingerprint at every start, the Open
  to LAN screen shows the host's, and the game writes the pinned one into its log at the first join
  (`First connection to … : its certificate … is pinned`). Whoever wants to rule out a man in the
  middle at the first join compares the two over another channel.
- **One TLS library everywhere:** Qt's `openssl` backend on every platform, with the OpenSSL the
  game ships (3.5, an LTS line; `3rdparty/openssl-version.txt`). The game initialises it without
  reading any configuration file (neither `OPENSSL_CONF` nor one of the system changes its TLS). The libraries are part of the release (`QtCraft.app/Contents/Frameworks`), so
  the game never uses an OpenSSL of the system or of Homebrew, and the server's key never enters a
  keychain. A game whose OpenSSL is missing logs "No TLS, multiplayer is not possible" at its start
  and cannot host or join; single player still works.
- **Builds without TLS:** a build configured with `-DQC_TLS=OFF` (for a platform without OpenSSL
  yet) talks plain TCP: nothing is encrypted and nothing pinned, the dedicated server says so at its
  start. It cannot talk to a normal build.
- **Older versions:** a client before protocol 75 cannot talk to a newer server and the other way
  round; the newer client says "The server did not set up an encrypted connection (does it run the
  same version of QtCraft?)".
- **Costs:** see decision E-872 (handshake time and CPU per player, measured headless).

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

- **The first connection to an address is trusted blindly** (trust on first use). Whoever sits
  on the path at exactly that moment (the same Wi-Fi, a provider) can show his own certificate; the
  client pins it and talks to him. Later connections are protected: another certificate is refused.
  Compare the fingerprint with the server's owner to be sure. Everything else, chat included, is
  encrypted; a recorded connection reveals neither the identity nor anything to guess the password
  from.
- **Whoever has `server-key.pem` is the server** for every player who pinned it. Keep the data folder
  to yourself; a copied key lets someone impersonate the server.
- **Windows: keep the data folder inside your user profile** (the default under `%APPDATA%`, or a
  folder below `C:\Users\<you>`), not in the root of a drive such as `C:\` or `D:\`. Secret files
  (`server-key.pem`, `identities.json`, `server.json`, the player key, `known-servers.json`) get an
  owner-only access list right after they are written; until then, and on drives without access lists
  (FAT, many USB sticks; the program then warns), they have the access list of their folder, and the
  root of a drive lets every user of the computer read (decision E-901).
- **A hostile server** learns what a player sends it: the identity for that server (the client
  sends it whenever the challenge says the name is not known yet), chat and actions. It cannot learn
  the identity for another server unless it presents that server's salt (see below).
- **A forgotten pin is trusted anew.** "Forget the old certificate" is meant for a server whose owner
  really made a new one; used against a real attack it hands the connection to the attacker.
- **A hostile server** could present the salt of another server and so learn the player's identity
  for that other server (only for players who join it).
- **The key is the player.** Whoever copies `player.key` can play as that player everywhere; whoever
  loses it starts as a new player (a server then refuses the old name until an operator helps,
  e.g. by removing the name from `identities.json`). Deleting the key also escapes bans by
  fingerprint.
- **Trust on first use:** a name nobody has used on a server yet belongs to whoever uses it first.
  Operators named in advance (`/op Name`, `operators` in `server.json`) are bound at that first
  login.
- **Operators can change the server from afar** (decision E-848): over the admin channel they set
  server limits (a dedicated server writes them into its `server.json`), game rules and the lists,
  and stop the server. Make only people operators whom you would hand the server's console.
- Limits on messages, frame sizes, connections per address and the login time protect the server
  from floods and broken clients, not from many machines at once. The limits per address count an
  IPv6 client by its /64 network (a host picks the rest of its address freely), an IPv4 client by
  its address (a whole household behind one NAT shares the limits).

## Server administration (admin channel)

Operators manage a running server over their normal game connection (decision E-848); there is
no second port and no separate login. In the game, operators open **Pause menu → Manage server**
(decision E-863): overview with charts of the load, players (operator rights, kick and ban with a
reason), chat, commands with completion and parameter help, lists, game rules and limits (edit,
then save or discard), log with a level filter, saving, backups and stopping. The entry shows only
to operators: the server tells every player when he becomes one and when he stops being one (an
admin result with number 0). The screen follows status, chat and log only while it is open. There
is no management-only connection from the main menu: join the server (as a spectator if you like)
and open the screen there. The control interface uses the same channel (`admin`, `adminFollow`,
`adminStatus`, see `docs/Steuerschnittstelle.md`).

- **Who:** operators only, the same rule as the commands (`access.json`, `operators` in
  `server.json`, the local player of a single-player world). The server checks the rights on
  **every** admin message and on every tick for the topics followed: a player who is no operator
  gets a refusal and a warning in the log (`qtcraft.admin`), and `/deop` ends an operator's
  subscription at once.
- **Status:** following the status sends the history first (the last 10 minutes at one point a
  second, the last 24 hours at one point a minute; recorded all the time), then once a second the
  tick mean, 99th percentile and maximum, ticks per second, the mean per tick phase, players,
  loaded chunks, entities by kind, memory (resident and peak), network rates, uptime, world,
  seed, generator, version and protocol, together with the player list. While an operator
  follows the status the server pings every player every 5 s, so the list shows round trips.
- **Players:** name, ping, position, game mode, operator flag, local or network, connection time,
  address; kick (with reason), ban (with reason), unban, op, deop, whitelist add/remove/on/off.
- **Chat:** the last 200 chat lines (players and `/say`) on subscribing, then each new line;
  sending a line as the server runs `/say`.
- **Commands:** a catalogue of all commands and macros (name, aliases, permission, usage and
  description as translation keys); the client reads the parameters from the usage
  (`QcCommandSyntax`: literals, required and optional words, choices, types such as player,
  coordinate, block, item) for a picker with completion. A command line runs as the operator; its
  replies come back in the answer instead of the chat.
- **Lists:** operators, whitelist (and whether it is on), bans with reason, who banned and since
  when.
- **Settings:** the game rules (`/gamerule`) and the server limits (`/limits`), each with group,
  range, default, current value and a translated description. Changes run the commands: rules are
  kept in `world.json` with the next save, limits of a single-player world too (only the keys an
  operator set), limits of a dedicated server are written into the `limits` of its `server.json`
  at once (its other keys stay as they are). `/limits set <key> <value>` and `/limits reset <key>`
  do the same from the chat or the console.
- **Log:** the tail of the server's log (the last 2 000 lines kept in memory, from the chosen
  level on, at most 2 000 per message, one tail a second per operator) and, while followed, the
  new lines once a second, each line once. A dedicated server shows its whole log. A world hosted
  by the game (opened to the LAN) shares its log with the host's own game, so remote operators see
  only the server's categories there (`qtcraft.admin`, `commands`, `gamemodes`, `items`, `lag`,
  `network`, `plants`, `server`, `trades`, `worldgen`); the host's client, settings, folders
  (`qtcraft.worlds`, `qtcraft.content`) and the servers he joined stay hidden.
- **Save, backup, stop:** run `/save` and `/backup [label]`; stopping runs the console's `/stop` and
  needs the argument `confirm`. An operator may stop the server this way although `/stop` itself
  is the console's command, because remote management (the server on one machine, the operator at
  another) is the point of the channel; the log names who stopped it.

A refusal says why (`AdminResult::Refusal`): missing rights (every subscription has ended then),
wrong arguments, a stop without confirmation, or a request repeated too soon; texts are never cut
silently: request texts beyond their bounds are refused by the decoder, replies carry arguments of
up to 1 024 characters.

Every action is the command an operator could type, so the channel has no rules of its own. The
admin messages share a rate limit (5 per second, bursts of 20; the local player is exempt like his
chat) and bounds on every count and text (`QcMsg::AdminLimits`); a frame beyond them closes the
connection. Clients of protocol versions before 67 are refused at the login as for every version
difference; older clients have no admin channel. The channel adds no protection of its own: it is as safe as the connection it runs
on (TLS, pinned at the first join; see "Limits of this security").

## Network behaviour

- Frames are length-prefixed; a keep-alive goes out after 5 s without traffic, a connection without
  traffic for 30 s counts as lost: the player is removed and saved.
- A client that cannot keep up gets new chunks only while less than 4 MiB wait to be sent, and is
  cut off beyond 64 MiB: the server logs "The connection is too slow", the player only sees the
  connection lost (a reason could not get past the 64 MiB waiting before it).
- The own player is predicted by the client and corrected by the server; with round trips of 50 to
  300 ms walking, jumping and turning needed no correction in the tests (E-257). Breaking and
  placing show at once too (E-853): the client predicts the blocks, numbers the action, and the
  server's acknowledgement (`BlockAck`, after the blocks it set and the next world sync with the drops) confirms them or rolls them back to
  the server's blocks (out of reach, mode, item gone, another player first); only a rollback is
  meshed again (`test_block_prediction`).
- Game modes are per player and kept in the player file (E-282). Everyone can ask for their own
  with `/gamemode`; operators change them with `/gamemode <survival|creative|adventure|spectator> [player]`; the server checks every creative action itself: it
  accepts flight only for players in creative mode (the shared physics runs with the mode's rules,
  so a client that sends the wish to fly in survival just jumps), refuses catalogue items outside
  creative mode and breaks instantly only for creative players. Flying, instant breaking and the
  catalogue were tested over a lagging link (round trips of 50 to 300 ms) without any correction
  (`test_creative_client`), the refusals over TCP (`test_network_game`, `test_creative_bots`).
- Adventure mode (E-735) is survival without breaking and placing except where the world's
  allow-lists permit it (`/adventure`, saved in `world.json`). The server sends the lists with the
  mode to adventure players and refuses every mining and every use of an item on a block they do
  not allow; the client applies the same rule from common code (`QcAdventureRules::permits`), so it
  neither shows cracks nor sends a refused request, and nothing has to be corrected
  (`test_adventure_mode`).
- Spectators (`/gamemode spectator`, E-730) fly through blocks: the shared physics runs without
  collision for them on both sides, so the server never pushes a spectator out of a wall and the
  prediction needs no correction (`test_creative_client`). The server sends a spectator to nobody
  (other players do not see him, also not other spectators), creatures ignore him, and he presses
  no plate, picks up nothing and is hit by nothing; every request that would touch the world or
  the inventory is dropped (`test_spectator_mode`). The player list still names him, marked and
  greyed at its end (`PlayerList` carries the mark); chunks load around him as around any player.
- Server and client treat a chunk that is not there yet as a wall. A player flying faster than the
  server generates stops at the edge on both sides without a correction; only a chunk the server
  has but the client has not received yet makes the server correct the prediction once it arrives
  (nearest chunks are sent first, up to 32 per tick, so this needs a very slow link) (E-284).
- Below the bottom of the world (y < 0) there is no wall, loaded or not: both sides let a player fall
  (or a creative player fly freely, also under unloaded columns) into the void with the same shared
  physics, which stops a body sinking at y = −256, so falling or flying down there needs no correction
  (`test_void_client`, round trips of 50 to 300 ms). The movement check is otherwise unchanged; the
  server alone deals the void damage below y = −64 (counted in ticks, so it follows `/tick rate`) and
  removes everything else there; a spawn column without any block gets one under the spawn height; a bot dies in
  the void over TCP in `test_creative_bots` (E-288).
- Catalogue items reach the world only when a player puts them there on purpose (E-283): throwing
  them (`Q`) and putting them into a chest work as in survival, but a creative player's death drops
  nothing (whatever `keepInventory` says), and when a player leaves creative mode the items of the
  crafting grid that no longer fit into the creative inventory are deleted instead of thrown.
  Creative players do not eat: the food stays in the hand and the frozen survival hunger unchanged,
  also through death and respawn.

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
- Farming (E-289 to E-296, protocol 29): crop stages, farmland moisture and fruit are block states in
  the usual `BlockUpdate`s, young or enamoured animals are two bits of the existing entity flags
  (`Young`, `InLove`), and the content sync carries each animal's breeding food so the client knows
  when a click on a creature is a use of the creature.
- Signal Dust (E-333 to E-339, protocol 30): circuits run only on the server. Dust strengths and
  connections, lit torches and lamps, repeater delays, levers, buttons, plates, open doors and piston
  states are block states in the usual `BlockUpdate`s; the content sync carries each block's signal
  kind, so the client uses levers, buttons and repeaters instead of placing against them. Blocks a
  piston moves are at their new cells in the server's world at once; the clients see the source cells
  empty, the block as a sliding entity (`EntitySpawn` of the kind `MovingBlock`) for two ticks and then the
  new cells. Bodies in the way are pushed by the server: the pushed player gets the new position with
  the next `PlayerState`, like knockback; the client predicts nothing of pistons. Levers, buttons and
  repeaters switch at most 5 times per second per player (burst 10; bucket `switch`, beyond it a click
  does nothing). The work of all circuits per tick is bounded (`signalBudget`, see Limits):
  every step takes only part of what is left, only a single dust region or piston push can still
  cross it; the rest waits for the next tick. Pushed players stop at walls and are moved out of blocks
  by the server (a correction like knockback); for two ticks the client sees a moved block's new cell
  empty, so walking into it there is corrected too. Chest and furnace screens close when a piston moves
  their block.
  The `debug` data has the tick phase `signal`. Tests: `test_signal_bots` (dedicated server and two
  bots over TCP: lever, dust, repeater and lamp; a piston door closes and opens).
- Signal machines (E-342 to E-348, protocol 31): hoppers, comparators, observers, dispensers and
  droppers run only on the server. Their states (a hopper locked, a comparator's mode and strength,
  an observer's pulse, a dispenser triggered) are block states in `BlockUpdate`s; the content sync
  carries each block's container columns, the placement rule `into` (a hopper's spout) and the new
  signal kinds, so a comparator is used (mode) instead of placed against. Their contents are those of
  a chest: the screen (`OpenMachine`, `MachineState` with the slots, the layout from the block's
  columns) shows what hoppers and dispensers move as they move it, and every player looking into one
  sees the same. Hoppers do their work in the signal phase under their own bound (4096 acting
  hoppers per tick); idle hoppers sleep and cost nothing. A piston that moves a hopper, dispenser or
  dropper moves its contents and cooldown and closes the screens open on it. Tests:
  `test_machine_bots` (dedicated server and two bots over TCP: a hopper sorter filled through the
  screens sends wheat to its chest and dirt on, an observer clock ticks for both bots, a dispenser
  pours water both see, a piston moves an open hopper: the screen closes and the items moved along).
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

**Latency** (E-886): every client measures its round trip to the server with `LatencyProbe` and
`LatencyEcho` (every 0.5 s, answered by the server at once on its thread, not on its tick). The
debug screen shows it in the general column (`Latency: 0.4 ms (last … · min … · max … · jitter … ms)`,
one decimal below 10 ms), the player list shows each player's reported value in milliseconds with
signal bars like the reference (5 below 150 ms, 4 below 300, 3 below 600, 2 below 1000, else 1).
Bots report it through the control commands `latency` and `players` (`latencyMs`), in `debug` and
the statistics log (`client.latency`), and in their `HEALTH` line (`latency … ms (min, max,
jitter)`; `--health-log <s>`), so network tests and measurements can log it.
The echo queues behind the data the server has already written to the connection, so the round
trip includes the server's send backlog (chunk streaming after a join, a burst of block updates),
not only the network; a probe waiting longer than the smoothed value shows as a lower bound
(`Latency: ≥ X ms`, `pendingMs`), so a stall is visible at once. Clients report their round trip at
display resolution (0.1 ms below 10 ms, whole milliseconds above), and the server lists a new value
only when it shows (at least 1 ms and 10 %, another signal bar, or unknown/known), at most every 2 s;
a quiet game sends no player lists. Probes are rate limited to bursts of 4 at 4 per second (two per
second are sent); a probe over the limit gets no echo and counts as lost.
`test_latency::roundTripIsMeasured` checks it in single player and over links delayed by 100 ms each
way (with and without 50 ms jitter): after the busy start is forgotten, the smallest round trip lies
between twice the delay and that plus jitter and 150 ms (larger ones and the smoothed value get a
second more for the load of parallel tests), and the server lists it with the player.

`test_multiplayer_bots` starts a dedicated server and four bots as processes of their own on
127.0.0.1 (random free port) and checks through the control interface: everyone sees everyone
(player list and entities), an operator made on the server console can use `/time` while another
player is refused, the time of day agrees on all clients, look, armour and animation (sneaking,
walking speed) reach the others, chat reaches everyone, a block mined and placed by hand changes
for all, water flows and a torch lights its surroundings on every client, a player who leaves
disappears for the others and is saved, rejoining restores position and inventory, a second
installation cannot take a name in use, and stopping the server ends the bots with code 4. It polls
for each condition (60 s at most) instead of waiting fixed times; a run takes about 16 s.

`test_creative_bots` runs the creative mode the same way with a survival server and two bots: in
survival the catalogue and flight are refused, only the operator may change her mode, in creative
mode she flies (the other bot sees her up in the air), lands without damage, takes items from the
catalogue, picks a block and breaks it at once (the other bot's world loses it too), and changing
back to survival brings back her survival inventory; about 6 s.

## Backups

`/backup [label]` (operators, console) saves the world, freezes its files with hard links in a
moment and copies them in the background; the game goes on meanwhile and the backup holds the world
as it was at the command. The backup folder appears under `<data>/backups` when the copy is
complete.
