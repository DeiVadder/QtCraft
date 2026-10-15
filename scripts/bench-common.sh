# Shared by bench.sh, bench-ab.sh, bench-headless.sh, profile.sh and perf-check.sh (sourced, not
# run). Defines the benchmark scenarios (scene_of: world, spawn, look and chat lines, the same for
# the game and for the headless measurement) and how one run of the game is started (run_game).
#
# Scenarios:
#   stand    - standing still at the spawn
#   walk     - walking straight ahead (no sprint), streaming new chunks slowly
#   sprint   - sprinting and jumping straight ahead, streaming new chunks
#   crowd    - standing among summoned mobs (humanoid zombies included) and dropped items
#   light    - standing on a cleared stone floor while torches appear and vanish around the player
#              four times a second (block light and remeshing in the background)
#   liquids  - standing on a cleared stone floor while water and lava sources appear above it and
#              vanish again twice a second (flow steps on the server, liquid meshing)
#   glass    - standing at night on a cleared log floor inside three square walls of transparent
#              blocks (panes at 4 m, glass at 7 m, leaves and ice at 10 m) with eight lamps, while
#              patches of light sources and transparent blocks appear and vanish around the player
#              twice a second (translucent sorting, cutout meshing, block light, remeshing). Uses
#              only blocks from before the block expansion, so it also runs on older builds (A/B)
#   glass-full - the same with the blocks of the block expansion: stained glass and panes, iron
#              bars, firefly glass, lanterns, small torches and cobwebs (newer builds only; an
#              older build answers /fill with an unknown block with a usage line and skips it)
#   showcase - standing in the showcase world (stage 21) between the first rows of areas, looking
#              north over the creature pens and the podiums of the building parts: one persistent
#              creature of every kind (six in the base game), hundreds of labels in reach, many
#              partial shapes and cutout blocks; the world is the same for every seed (--showcase)
#   flat-<scenario>, void-<scenario> - any scenario above in a flat world (the default layers:
#              grass on top at y 63, no trees, caves or creatures of their own accord) or in the void
#              world (a 5 x 5 platform at y 63, noon): the same scene whatever the terrain generator
#              does, so changes of the world generation (E-101, E-214) do not move the comparison.
#              WORLD_TYPE=flat|void does the same for every scenario of a run. Walk, sprint,
#              armour-walk and crowd need ground: off the platform the player and the summoned
#              creatures fall into the void and die or vanish (decision E-288), so they run in the
#              flat world instead (with a note on stderr)
#   armour-crowd - flat-crowd plus 40 armour stands in a full diamond set with the green cape
#              (/summon armor_stand, all on one cell 3 m ahead) and 8 caped mannequins: entity
#              meshing of armour attachments and capes, the stand sync per client and tick. Runs in
#              the flat world unless WORLD_TYPE or a prefix says otherwise (stage 23 and newer)
#   armour-walk - walking like walk, the own player in a full diamond set with the green cape,
#              seen from behind (--camera behind): third-person figure with attachments and the
#              cape swing (stage 23 and newer)
#   gravel-collapse - standing in a flat world on three layers of gravel under the grass while,
#              two seconds in, a shaft beside the player opens down to the void: the gravel of the
#              loaded chunks collapses layer by layer into the void (falling blocks at the manager's
#              limit of 2048, deferred falls, the per-tick budget of block updates; decision E-288).
#              Measures the server tick under many falling entities. Always flat (stage 26b and newer)
#   farm     - standing by day in a flat world in the middle of a 63 x 63 field of wet farmland
#              (water rows every 9 m) with young wheat and, in one strip, pumpkin and melon stems
#              beside free farmland for their fruit; random ticks on. From three seconds in, a /tick sprint
#              of 600 ticks (30 s of game time, a few milliseconds) every second: crops grow, stems
#              bear fruit, the client remeshes the field (phase plants, block changes). Tick values
#              include the sprint ticks. Stage 27 and newer
#   farm-seeds - the same field, but all wheat ripe and no sprint, with seed stacks lying on the
#              wheat (the player teleports over the field and drops them): every resting stack
#              looks for free soil once a second and finds none (cost of self-planting, phase
#              items; decision E-294). Stage 27 and newer
#   inventory, furnace, handbook - standing with the inventory, a filled furnace (looking down at
#              it) or the handbook page of stone open (QML screens over the world)
#   showcase-<demo> - standing in the showcase world at the lookout of a demo structure, e.g.
#              showcase-signal_components (dust and lamp clocks, stage 29.1), showcase-signal_machines
#              (hoppers and the sorter, stage 29.2), showcase-signal_farm (the repeating water farm):
#              the stop "demo-<demo>" that the build's own qtcraft-headless --showcase-tour gives
#              (areas move when content changes). A build without that demo produces no result
#   liquid-fill - standing in a flat world on a stone wall while a 14 x 33 sheet of water sources
#              west of it and one of lava east of it appear 6 m up and vanish again, one change a
#              second: liquids fall and spread at the step budget, recede, and lava meets water
#              (phase liquids, block changes through the block-entity path)
#   signal-stress - looking down from a block 10 m up in a flat world at Signal Dust contraptions
#              that run on their own (stage 29 and newer): a 48 x 48 dust field 21 to 68 m north,
#              toggled every 2 ticks by 16 observer clocks inside it (two observers facing each
#              other); 200 repeater clocks (a torch on a block, its dust through a repeater of delay 4
#              back into the block: 20 ticks a period, 3 times out in 60 ticks, so the torch never
#              burns out) with two lamps each, in two squares of 40 m left and right of the field
#              (lamps on for 10 ticks and off for 10, longer than their off delay of 4: light changes
#              and remeshing); 32 more repeater clocks each moving two sticky pistons with stone on
#              top up and down, in a row 15 to 17 m ahead (moving blocks, block updates). Phases
#              signal and blockUpdates, light jobs, remeshing. About 2000 /setblock lines at the start
#   flight   - creative mode, flying east from the spawn at 20 m/s at y 200 through new
#              terrain (teleports every 250 ms, so the speed does not depend on the physics): chunk
#              generation, arrival and unloading for the whole run. Tick lateness, memory and the
#              chunk phases over time come from STATS lines (STATS=<ms>, see below)
#   explore  - the exploration flight (stage 31.3): the flight above, but at EXPLORE_STEP metres
#              every 250 ms (default 10: 40 m/s), and once a second a 3 x 3 glass platform at y 70
#              below the player, as a player who builds along the way: every chunk passed is changed
#              and has to be saved and unloaded once out of view (memory, unloading, background
#              saving). In the window 75 s with STATS=5000 (15 points of the memory curve, 3 km of
#              new terrain; EXPLORE_STEP=5 and 300 s is the flight of the stage 31.3 measurements);
#              its server side headless: scripts/bench-headless.sh (decision E-388)
#   showcase-flight - the same flight in laps of 80 s around a 400 m square that covers the whole
#              showcase world and its surroundings, so every chunk leaves view and comes back once
#              a lap: whether block entities and memory stay bounded (runs of one and three laps)
#
# TEXTURE_PACKS=<ids> runs with these texture packs (QTCRAFT_TEXTURE_PACKS, stage 28, decision E-304),
# highest priority first, e.g. builtin:qtcraft_hd_32, builtin:qtcraft_hd_64, builtin:qtcraft_hd_128,
# builtin:storybook_32 or "none" (default: none, the game's 16 x 16 textures). The resolution follows
# from the packs; the log line "Texture atlas:" gives the atlas size, mip levels, memory and build time.
#
# PACK_SWITCH=<ids> switches these texture packs on a third into the run and off again at two
# thirds (QTCRAFT_BENCHMARK_PACK_SWITCH, decision E-332); the log gets a PACKSWITCH line per switch.
# The player's stored pack selection is not changed. Older builds ignore it.
#
# PREVIEW_ACTIVE=1 animates the character previews (inventory, main menu) although the window of a
# scripted run is inactive (QTCRAFT_PREVIEW_ACTIVE, decision E-231); otherwise they draw one still
# picture, as in a game whose window lost the focus.
#
# STATS=<ms> writes the debug data as a STATS line into the run's log every <ms> milliseconds
# (QTCRAFT_STATS, decision E-225): values over time, e.g. memory and tick lateness of a flight.

# Display awake while the calling script runs; runs wait while the screen is locked (screen-guard.sh).
. "${QC_SCRIPTS_DIR:-$PROJECT_DIR/scripts}/screen-guard.sh"
keep_awake

limited() {
    perl -e 'alarm shift; exec @ARGV or die' "$@"
}

CROWD_ITEMS="coal diamond gold_ingot iron_ingot stick raw_porkchop rotten_flesh slimeball wooden_sword stone_sword
iron_sword diamond_sword wooden_pickaxe stone_pickaxe iron_pickaxe diamond_pickaxe wooden_axe stone_axe iron_axe
diamond_axe wooden_shovel stone_shovel iron_shovel diamond_shovel"

# Blocks of the glass scenarios: the walls at 4, 7 and 10 m (one block per side: +x, -x, +z, -z),
# the eight lamps and the eight toggled patches (light sources on floor rows, transparent blocks
# in upright 3 x 3 patches).
GLASS_WALLS="glass_pane glass_pane glass_pane glass_pane glass glass glass glass oak_leaves birch_leaves spruce_leaves ice"
GLASS_LAMPS="torch mine_lamp torch mine_lamp mine_lamp torch mine_lamp torch"
GLASS_TOGGLES="torch glass mine_lamp glass_pane torch oak_leaves mine_lamp glass"
GLASS_FULL_WALLS="red_stained_glass_pane blue_stained_glass_pane iron_bars lime_stained_glass_pane yellow_stained_glass
cyan_stained_glass firefly_glass magenta_stained_glass oak_leaves cobweb spruce_leaves white_stained_glass"
GLASS_FULL_LAMPS="lantern small_torch firefly_glass torch small_torch lantern torch firefly_glass"
GLASS_FULL_TOGGLES="small_torch red_stained_glass lantern blue_stained_glass_pane firefly_glass birch_leaves torch cobweb"

# Word $1 (counted from 1) of the remaining arguments.
word() {
    shift "$1"
    echo "$1"
}

# Prints the chat lines of a glass scenario, one per line.
# Usage: glass_chat <walls> <lamps> <toggles> <seconds>
# The lamps follow over the warm-up and the toggles come twice a second, each changing a row or patch
# of blocks. (Paced below the chat limit of 2 a second, bursts of 20, which applied to the local
# player before the exemption of E-091; kept so older builds get the same load.) The floor is
# made of logs, so the leaves keep a trunk in reach and do not decay.
glass_chat() {
    walls=$1
    lamps=$2
    toggles=$3
    echo "/time set night"
    echo "/fill ~-11 ~-1 ~-11 ~11 ~-1 ~11 oak_log"
    echo "/fill ~-11 ~ ~-11 ~11 ~5 ~11 air"
    i=0
    for r in 4 7 10; do
        q=$((r - 1))
        # shellcheck disable=SC2086
        echo "/fill ~$r ~ ~-$r ~$r ~3 ~$r $(word $((i + 1)) $walls)"
        # shellcheck disable=SC2086
        echo "/fill ~-$r ~ ~-$r ~-$r ~3 ~$r $(word $((i + 2)) $walls)"
        # shellcheck disable=SC2086
        echo "/fill ~-$q ~ ~$r ~$q ~3 ~$r $(word $((i + 3)) $walls)"
        # shellcheck disable=SC2086
        echo "/fill ~-$q ~ ~-$r ~$q ~3 ~-$r $(word $((i + 4)) $walls)"
        i=$((i + 4))
    done
    i=1
    for spot in "5 5" "-5 5" "5 -5" "-5 -5" "8 8" "-8 8" "8 -8" "-8 -8"; do
        # shellcheck disable=SC2086
        echo "@$((i * 300)) /setblock ~${spot% *} ~ ~${spot#* } $(word "$i" $lamps)"
        i=$((i + 1))
    done
    end=$((3000 + $4 * 1000))
    at=3500
    step=0
    while [ "$at" -lt "$end" ]; do
        index=$(((step / 2) % 8))
        # shellcheck disable=SC2086
        block=$(word $((index + 1)) $toggles)
        [ $((step % 2)) -eq 1 ] && block=air
        case $index in
        0) area="~5 ~ ~-1 ~5 ~ ~1" ;;
        1) area="~-1 ~ ~8 ~1 ~2 ~8" ;;
        2) area="~-5 ~ ~-1 ~-5 ~ ~1" ;;
        3) area="~-1 ~ ~-8 ~1 ~2 ~-8" ;;
        4) area="~8 ~ ~-1 ~8 ~ ~1" ;;
        5) area="~-1 ~ ~5 ~1 ~2 ~5" ;;
        6) area="~-8 ~ ~-1 ~-8 ~ ~1" ;;
        *) area="~-1 ~ ~-5 ~1 ~2 ~-5" ;;
        esac
        echo "@$at /fill $area $block"
        step=$((step + 1))
        at=$((at + 500))
    done
}

# Prints the chat lines of a farm scenario, one per line. Usage: farm_chat sprint|seeds <seconds>
# The field covers x -31..31 and z -30..32 around the player on the layer below his feet: wet
# farmland with water rows at z -26, -17, -8, 1, 10, 19 and 28 (no cell is further than 4 m from
# water, so the soil stays wet), wheat on the strips between them and pumpkin and melon stems on
# every other row of the strip z 20..27 (the rows between stay free for the fruit). The water rows
# lie in trenches of farmland: the sources have nowhere to flow. 63 x 63 = 3969 cells: 441 water,
# 3528 farmland under 3024 wheat, 252 stems and 252 free cells.
farm_chat() {
    if [ "$1" = seeds ]; then
        wheat="wheat_crop[age=7]"
    else
        wheat=wheat_crop
    fi
    echo "/time set day"
    # The flat world is empty above the grass, so nothing needs clearing (a clearing /fill of
    # 63 x 4 x 63 blocks would exceed the 4096 a /fill may cover without /confirm and wait for it).
    echo "/fill ~-31 ~-1 ~-30 ~31 ~-1 ~32 farmland[moist=true]"
    for z in -26 -17 -8 1 10 19 28; do
        echo "/fill ~-31 ~-1 ~$z ~31 ~-1 ~$z water"
    done
    for strip in "-30 -27" "-25 -18" "-16 -9" "-7 0" "2 9" "11 18" "29 32"; do
        echo "/fill ~-31 ~ ~${strip% *} ~31 ~ ~${strip#* } $wheat"
    done
    for row in "20 pumpkin_stem" "22 pumpkin_stem" "24 melon_stem" "26 melon_stem"; do
        echo "/fill ~-31 ~ ~${row% *} ~31 ~ ~${row% *} ${row#* }"
    done
    if [ "$1" = sprint ]; then
        at=3000
        while [ "$at" -lt $((3000 + $2 * 1000)) ]; do
            echo "@$at /tick sprint 600"
            at=$((at + 1000))
        done
        return
    fi
    # 150 seed stacks on a grid over the wheat strips (15 in x, 4 m apart, on 10 rows, two per
    # strip): teleport there (relative steps) and drop them up to 2 m ahead, still on the strip and
    # off the water (items in water drift and never rest); the stacks lie more than 1 m apart, so
    # they do not merge. Back to the start at the end.
    x=0
    z=0
    for gz in -23 -20 -14 -11 -5 -2 4 7 13 16; do
        gx=-28
        while [ "$gx" -le 28 ]; do
            echo "/tp ~$((gx - x)) ~ ~$((gz - z))"
            echo "/spawnitem wheat_seeds 3"
            x=$gx
            z=$gz
            gx=$((gx + 4))
        done
    done
    echo "/tp ~$((-x)) ~ ~$((-z))"
}

# Prints the chat lines of a flight scenario, one per line. Usage: flight_chat terrain|explore|showcase <seconds>
# Creative mode (no fall damage, nothing attacks); from three seconds in a teleport every 250 ms moves
# the player 5 m on (20 m/s; explore: EXPLORE_STEP m, default 10; a teleport also stops the fall in
# between). terrain and explore fly east from the spawn at y 200; showcase flies laps of 80 s at
# y 110 around the square of corners (-130, 50) and (270, -350), which holds the whole showcase world
# (x 0 to 160, z -260 to 10) with 110 m to spare on every side, more than the view distance of
# 8 chunks: every chunk leaves and comes back once a lap.
flight_chat() {
    echo "/gamemode creative"
    echo "/time set day"
    at=3000
    step=0
    while [ "$at" -le $((3000 + $2 * 1000)) ]; do
        if [ "$1" = explore ] && [ $((step % 4)) -eq 0 ]; then
            echo "@$at /fill ~-1 70 ~-1 ~1 70 ~1 glass"
        fi
        if [ "$1" = terrain ]; then
            echo "@$at /tp ~5 200 ~"
        elif [ "$1" = explore ]; then
            echo "@$at /tp ~${EXPLORE_STEP:-10} 200 ~"
        else
            # 320 steps a lap, 80 along each side.
            side=$(((step % 320) / 80))
            along=$(((step % 80) * 5))
            case $side in
            0) echo "@$at /tp $((-130 + along)) 110 50" ;;
            1) echo "@$at /tp 270 110 $((50 - along))" ;;
            2) echo "@$at /tp $((270 - along)) 110 -350" ;;
            *) echo "@$at /tp -130 110 $((-350 + along))" ;;
            esac
        fi
        at=$((at + 250))
        step=$((step + 1))
    done
}

# Prints the chat lines of the liquid-fill scenario, one per line. Usage: liquid_fill_chat <seconds>
# A stone wall 5 m wide, 8 m high and 49 m long through the player, who then stands on top of it;
# from three seconds in, once a second: water sources on x -16..-3, lava sources on x 3..16 (both z
# -16..16, 6 m above the ground), then the water sources and the lava sources gone again (4 s a cycle).
liquid_fill_chat() {
    echo "/time set day"
    echo "/fill ~-2 ~ ~-24 ~2 ~7 ~24 stone"
    echo "/tp ~ ~8 ~"
    at=3000
    step=0
    while [ "$at" -lt $((3000 + $1 * 1000)) ]; do
        case $((step % 4)) in
        0) echo "@$at /fill ~-16 ~-2 ~-16 ~-3 ~-2 ~16 water" ;;
        1) echo "@$at /fill ~3 ~-2 ~-16 ~16 ~-2 ~16 lava" ;;
        2) echo "@$at /fill ~-16 ~-2 ~-16 ~-3 ~-2 ~16 air" ;;
        *) echo "@$at /fill ~3 ~-2 ~-16 ~16 ~-2 ~16 air" ;;
        esac
        at=$((at + 1000))
        step=$((step + 1))
    done
}

# Prints the chat lines of the signal-stress scenario, one per line (see the scenario above).
# Observer clock: observer facing east at x, observer facing west at x + 1; each sees the other
# change and pulses backwards (x - 1 and x + 2) for 2 ticks, so the pair runs once the second is
# placed. Repeater clock at x, z (as in test_signal_measure): block at x, z, torch on its east side,
# dust at x + 1, z + 1 and z + 2 and at x, z + 2, repeater (delay 4) at x, z + 1 facing north back
# into the block; placed block first and repeater last, so it starts at once. The torch drives what
# stands on top of it and east of it (lamps, or sticky pistons facing up with stone on top).
signal_stress_chat() {
    echo "/time set day"
    # Dust field x -24..23, z -68..-21; feeding pairs at x -18/-6/6/18 (and +1), z -62/-50/-38/-26.
    echo "/fill ~-24 ~ ~-68 ~23 ~ ~-21 signal_dust"
    for facing in east west; do
        for x in -18 -6 6 18; do
            [ "$facing" = west ] && x=$((x + 1))
            for z in -62 -50 -38 -26; do
                echo "/setblock ~$x ~ ~$z signal_observer[facing=$facing]"
            done
        done
    done
    # Lamp clocks: 10 x 10 in each square, x -66..-27 and 27..66, z -66..-27, 4 m apart.
    for left in -66 27; do
        i=0
        while [ "$i" -lt 10 ]; do
            j=0
            while [ "$j" -lt 10 ]; do
                stress_clock $((left + 4 * i)) $((-66 + 4 * j)) signal_lamp
                j=$((j + 1))
            done
            i=$((i + 1))
        done
    done
    # Piston clocks: 32 in a row, x -64..63, z -17..-15.
    i=0
    while [ "$i" -lt 32 ]; do
        stress_clock $((-64 + 4 * i)) -17 "sticky_signal_piston[facing=up]"
        i=$((i + 1))
    done
    echo "/setblock ~ ~9 ~ stone"
    echo "/tp ~ ~10 ~"
}

# One repeater clock of signal-stress at x $1, z $2 driving two blocks $3 (pistons get stone on top).
stress_clock() {
    x=$1
    z=$2
    echo "/setblock ~$x ~ ~$z stone"
    echo "/setblock ~$((x + 1)) ~ ~$z signal_torch[attach=west]"
    case $3 in
    sticky_signal_piston*)
        echo "/setblock ~$((x + 1)) ~2 ~$z stone"
        echo "/setblock ~$((x + 2)) ~1 ~$z stone"
        ;;
    esac
    echo "/setblock ~$((x + 1)) ~1 ~$z $3"
    echo "/setblock ~$((x + 2)) ~ ~$z $3"
    echo "/setblock ~$((x + 1)) ~ ~$((z + 1)) signal_dust"
    echo "/setblock ~$((x + 1)) ~ ~$((z + 2)) signal_dust"
    echo "/setblock ~$x ~ ~$((z + 2)) signal_dust"
    echo "/setblock ~$x ~ ~$((z + 1)) signal_repeater[facing=north,delay=4]"
}

# The qtcraft-headless of the build of app $1 (the qtcraft executable of a build directory).
headless_of() {
    echo "${1%/app/qtcraft.app/Contents/MacOS/qtcraft}/headless/qtcraft-headless"
}

# The camera stop "demo-<demo>" of the showcase tour as "x z yaw pitch", from qtcraft-headless $1
# (empty when the build has no such demo). Usage: showcase_stop <qtcraft-headless> <demo>
showcase_stop() {
    [ -x "$1" ] || return 0
    "$1" --showcase-tour --no-mods 2>/dev/null | awk -v name="demo-$2" '$1 == name { print $2, $3, $4, $5; exit }'
}

# Writes the chat lines of a scenario (one per line, "@<ms> " before a timed one) to stdout.
# Usage: scene_chat <scenario without prefix> <seconds> <armour>
scene_chat() {
    case "$1" in
    light)
        printf '%s\n' "/time set night" "/fill ~-11 ~-1 ~-11 ~11 ~-1 ~11 stone" "/fill ~-11 ~ ~-11 ~11 ~4 ~11 air"
        ;;
    liquids)
        # The second fill is larger than a /fill without /confirm may be (stage 24, decision E-245);
        # builds before stage 24 answer /confirm as an unknown command.
        printf '%s\n' "/fill ~-18 ~-1 ~-18 ~18 ~-1 ~18 stone" "/fill ~-18 ~ ~-18 ~18 ~6 ~18 air" "/confirm"
        ;;
    farm) farm_chat sprint "$2" ;;
    farm-seeds) farm_chat seeds "$2" ;;
    glass) glass_chat "$GLASS_WALLS" "$GLASS_LAMPS" "$GLASS_TOGGLES" "$2" ;;
    glass-full) glass_chat "$GLASS_FULL_WALLS" "$GLASS_FULL_LAMPS" "$GLASS_FULL_TOGGLES" "$2" ;;
    crowd)
        printf '%s\n' "/time set day" "/summon qtcraft:pig 16" "/summon qtcraft:pig 16" "/summon qtcraft:zombie 16" "/summon qtcraft:slime 8"
        for item in $CROWD_ITEMS; do
            echo "/spawnitem qtcraft:$item 1"
        done
        if [ "$3" = crowd ]; then
            i=0
            while [ "$i" -lt 40 ]; do
                echo "/summon armor_stand equip=diamond cape=green_cape arms"
                i=$((i + 1))
            done
            echo "/summon qtcraft:caped_mannequin 8"
        fi
        ;;
    inventory) printf '%s\n' "/give iron_pickaxe 1" "/give oak_planks 64" "/give torch 32" ;;
    furnace) printf '%s\n' "/give iron_ore 5" "/give coal 2" "/setblock ~ ~-1 ~ furnace" ;;
    signal-stress) signal_stress_chat ;;
    flight) flight_chat terrain "$2" ;;
    explore) flight_chat explore "$2" ;;
    showcase-flight) flight_chat showcase "$2" ;;
    liquid-fill) liquid_fill_chat "$2" ;;
    gravel-collapse) echo "@2000 /fill ~2 ~-4 ~ ~2 ~-1 ~ air" ;;
    esac
    if [ "$3" = walk ]; then
        echo "/equip diamond"
    fi
}

# Sets the scene of a scenario: what the game (run_game) and the headless measurement
# (scripts/bench-headless.sh) start. Usage: scene_of <qtcraft-headless> <scenario> <seconds> <chat file>
# Sets SCENE_WORLD (terrain, flat, void or showcase), SCENE_LAYERS (layers of a flat world or empty),
# SCENE_SPAWN ("x,z" or empty), SCENE_LOOK ("yaw,pitch"), SCENE_WALK (movement of the game's
# benchmark: stand, walk, sprint, light, liquids), SCENE_ARMOUR (crowd, walk or empty) and
# SCENE_WINDOW (options only the game knows: overlay, handbook page, camera), and writes the chat
# lines to the chat file. Returns 1 (with a note on stderr) when the build has no such showcase demo.
scene_of() {
    scenario=$2
    requested=$2
    SCENE_WORLD=${WORLD_TYPE:-}
    SCENE_LAYERS=
    SCENE_SPAWN=
    SCENE_LOOK=45,-10
    SCENE_WALK=stand
    SCENE_ARMOUR=
    SCENE_WINDOW=
    case "$scenario" in
    flat-* | void-*)
        SCENE_WORLD=${scenario%%-*}
        scenario=${scenario#*-}
        ;;
    esac
    case "$scenario" in
    armour-crowd)
        SCENE_ARMOUR=crowd
        SCENE_WORLD=${SCENE_WORLD:-flat}
        scenario=crowd
        ;;
    armour-walk)
        SCENE_ARMOUR=walk
        scenario=walk
        ;;
    gravel-collapse | farm | farm-seeds | liquid-fill | signal-stress)
        SCENE_WORLD=flat
        ;;
    esac
    case "$scenario" in
    showcase | showcase-flight)
        SCENE_WORLD=showcase
        SCENE_SPAWN=40,-19
        SCENE_LOOK=345,-8
        [ "$scenario" = showcase-flight ] && SCENE_LOOK=270,-20
        ;;
    showcase-*)
        stop=$(showcase_stop "$1" "${scenario#showcase-}")
        if [ -z "$stop" ]; then
            echo "Scenario $requested: this build has no showcase demo ${scenario#showcase-}; no run" >&2
            return 1
        fi
        # shellcheck disable=SC2086
        set -- $stop "$@"
        SCENE_WORLD=showcase
        SCENE_SPAWN="$1,$2"
        SCENE_LOOK="$3,$4"
        shift 4
        ;;
    esac
    if [ "$SCENE_WORLD" = void ]; then
        case "$scenario" in
        sprint | walk | crowd)
            echo "Scenario $requested needs ground; running it in the flat world instead of the void world" >&2
            SCENE_WORLD=flat
            ;;
        esac
    fi
    SCENE_WORLD=${SCENE_WORLD:-terrain}
    case "$scenario" in
    sprint | walk | light | liquids) SCENE_WALK=$scenario ;;
    furnace)
        SCENE_LOOK=0,-89
        SCENE_WINDOW="--overlay furnace"
        ;;
    signal-stress) SCENE_LOOK=0,-25 ;;
    gravel-collapse) SCENE_LAYERS="3*gravel,grass_block" ;;
    inventory) SCENE_WINDOW="--overlay inventory" ;;
    handbook) SCENE_WINDOW="--handbook block:qtcraft:stone" ;;
    esac
    [ "$SCENE_ARMOUR" = walk ] && SCENE_WINDOW="--camera behind"
    scene_chat "$scenario" "$3" "$SCENE_ARMOUR" >"$4"
}

# Runs one benchmark of the game.
# Usage: run_game <app> <scenario> <seconds> <view distance> <data dir> <log> [wrapper command...]
# The wrapper (e.g. a profiler) is put in front of the app. Prints the BENCH line. The run is killed
# after RUN_LIMIT seconds (default: its length plus 120 s).
run_game() {
    app=$1
    seconds=$3
    view=$4
    data=$5
    log=$6
    chat="$log.chat"
    scene_of "$(headless_of "$app")" "$2" "$seconds" "$chat" || return 0
    shift 6
    wrapper="$*"
    if [ "$SCENE_WORLD" = showcase ]; then
        set -- --showcase --view-distance "$view" --spawn "$SCENE_SPAWN" --look "$SCENE_LOOK"
    else
        set -- --play --seed 42 --view-distance "$view" --look "$SCENE_LOOK"
        [ "$SCENE_WORLD" = terrain ] || set -- "$@" --world-type "$SCENE_WORLD"
        [ -z "$SCENE_LAYERS" ] || set -- "$@" --flat-layers "$SCENE_LAYERS"
    fi
    while IFS= read -r line; do
        set -- "$@" --chat "$line"
    done <"$chat"
    # shellcheck disable=SC2086
    set -- "$@" $SCENE_WINDOW
    # Sound is mixed as in a normal game but muted, so runs stay quiet. GPU timestamps
    # (QSG_RHI_PROFILE) give the GPU time per frame. The window opens on the built-in display
    # (QTCRAFT_SCREEN overrides it) and stays in the background: no focus, no captured mouse.
    mkdir -p "$data"
    printf '[audio]\nmuted=true\n' >"$data/settings.ini"
    # DEBUG_DISPLAY=off|compact|full: level of the debug display during the run (default: the
    # game's default, compact; decision E-225).
    if [ -n "${DEBUG_DISPLAY:-}" ]; then
        printf '[debug]\nlevel=%s\n' "$DEBUG_DISPLAY" >>"$data/settings.ini"
    fi
    if [ "$SCENE_ARMOUR" = walk ]; then
        printf '[character]\ncape=qtcraft:green_cape\n' >>"$data/settings.ini"
    fi
    # Benchmarks measure the real window, never offscreen (E-229; the game refuses that anyway).
    # A run during which the screen was locked rendered no or too few frames: it is repeated (at most
    # twice) once the screen is unlocked again.
    attempt=1
    while :; do
        wait_for_screen || return 1
        # shellcheck disable=SC2086
        env QTCRAFT_OFFSCREEN=0 QTCRAFT_TEXTURE_PACKS="${TEXTURE_PACKS:-none}" QTCRAFT_BENCHMARK_PACK_SWITCH="${PACK_SWITCH:-}" QTCRAFT_PREVIEW_ACTIVE="${PREVIEW_ACTIVE:-0}" QTCRAFT_STATS="${STATS:-}" QSG_RHI_PROFILE=1 QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$data" QTCRAFT_BENCHMARK="$seconds" QTCRAFT_BENCHMARK_SCENARIO="$SCENE_WALK" \
            perl -e 'alarm shift; exec @ARGV or die' "${RUN_LIMIT:-$((seconds + 120))}" $wrapper "$app" "$@" >"$log" 2>&1
        if ! screen_locked || [ "$attempt" -ge 3 ]; then
            break
        fi
        echo "screen was locked during the run; repeating it ($log)" >&2
        attempt=$((attempt + 1))
    done
    screen_locked && return 1
    grep -o 'BENCH .*' "$log" | tail -1
}

# Median of the numbers on stdin.
median() {
    sort -n | awk '{ v[NR] = $1 } END { if (NR == 0) { print "-" } else if (NR % 2) { print v[(NR + 1) / 2] } else { print (v[NR / 2] + v[NR / 2 + 1]) / 2 } }'
}

# Value of key $1 in a BENCH line on stdin.
value() {
    sed -n "s/.* $1=\([0-9.]*\).*/\1/p"
}
