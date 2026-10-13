# Shared by bench.sh, profile.sh and perf-check.sh (sourced, not run). Defines the benchmark
# scenarios and how one run of the game is started.
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

# Runs one benchmark of the game.
# Usage: run_game <app> <scenario> <seconds> <view distance> <data dir> <log> [wrapper command...]
# The wrapper (e.g. a profiler) is put in front of the app. Prints the BENCH line. The run is killed
# after RUN_LIMIT seconds (default: its length plus 120 s).
run_game() {
    app=$1
    scenario=$2
    requested=$2
    seconds=$3
    view=$4
    data=$5
    log=$6
    shift 6
    wrapper="$*"
    look=45,-10
    walk=stand
    world=${WORLD_TYPE:-}
    case "$scenario" in
    flat-* | void-*)
        world=${scenario%%-*}
        scenario=${scenario#*-}
        ;;
    esac
    armour=
    case "$scenario" in
    armour-crowd)
        armour=crowd
        world=${world:-flat}
        scenario=crowd
        ;;
    armour-walk)
        armour=walk
        scenario=walk
        ;;
    gravel-collapse | farm | farm-seeds)
        world=flat
        ;;
    esac
    if [ "$world" = void ]; then
        case "$scenario" in
        sprint | walk | crowd)
            echo "Scenario $requested needs ground; running it in the flat world instead of the void world" >&2
            world=flat
            ;;
        esac
    fi
    case "$scenario" in
    sprint | walk) walk=$scenario ;;
    light | liquids) walk=$scenario ;;
    furnace) look=0,-89 ;;
    esac
    set -- --play --seed 42 --view-distance "$view" --look "$look"
    if [ "$scenario" = showcase ]; then
        set -- --showcase --view-distance "$view" --spawn 40,-19 --look 345,-8
    elif [ -n "$world" ]; then
        set -- "$@" --world-type "$world"
    fi
    case "$scenario" in
    light)
        set -- "$@" --chat "/time set night" --chat "/fill ~-11 ~-1 ~-11 ~11 ~-1 ~11 stone" --chat "/fill ~-11 ~ ~-11 ~11 ~4 ~11 air"
        ;;
    liquids)
        # The second fill is larger than a /fill without /confirm may be (stage 24, decision E-245);
        # builds before stage 24 answer /confirm as an unknown command.
        set -- "$@" --chat "/fill ~-18 ~-1 ~-18 ~18 ~-1 ~18 stone" --chat "/fill ~-18 ~ ~-18 ~18 ~6 ~18 air" --chat "/confirm"
        ;;
    farm | farm-seeds)
        if [ "$scenario" = farm ]; then
            chat=$(farm_chat sprint "$seconds")
        else
            chat=$(farm_chat seeds "$seconds")
        fi
        while IFS= read -r line; do
            set -- "$@" --chat "$line"
        done <<EOF
$chat
EOF
        ;;
    glass | glass-full)
        if [ "$scenario" = glass ]; then
            chat=$(glass_chat "$GLASS_WALLS" "$GLASS_LAMPS" "$GLASS_TOGGLES" "$seconds")
        else
            chat=$(glass_chat "$GLASS_FULL_WALLS" "$GLASS_FULL_LAMPS" "$GLASS_FULL_TOGGLES" "$seconds")
        fi
        while IFS= read -r line; do
            set -- "$@" --chat "$line"
        done <<EOF
$chat
EOF
        ;;
    crowd)
        set -- "$@" --chat "/time set day" --chat "/summon qtcraft:pig 16" --chat "/summon qtcraft:pig 16" \
            --chat "/summon qtcraft:zombie 16" --chat "/summon qtcraft:slime 8"
        for item in $CROWD_ITEMS; do
            set -- "$@" --chat "/spawnitem qtcraft:$item 1"
        done
        if [ "$armour" = crowd ]; then
            i=0
            while [ "$i" -lt 40 ]; do
                set -- "$@" --chat "/summon armor_stand equip=diamond cape=green_cape arms"
                i=$((i + 1))
            done
            set -- "$@" --chat "/summon qtcraft:caped_mannequin 8"
        fi
        ;;
    inventory)
        set -- "$@" --chat "/give iron_pickaxe 1" --chat "/give oak_planks 64" --chat "/give torch 32" --overlay inventory
        ;;
    furnace)
        set -- "$@" --chat "/give iron_ore 5" --chat "/give coal 2" --chat "/setblock ~ ~-1 ~ furnace" --overlay furnace
        ;;
    handbook)
        set -- "$@" --handbook block:qtcraft:stone
        ;;
    gravel-collapse)
        set -- "$@" --flat-layers "3*gravel,grass_block" --chat "@2000 /fill ~2 ~-4 ~ ~2 ~-1 ~ air"
        ;;
    esac
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
    if [ "$armour" = walk ]; then
        printf '[character]\ncape=qtcraft:green_cape\n' >>"$data/settings.ini"
        set -- "$@" --camera behind --chat "/equip diamond"
    fi
    # Benchmarks measure the real window, never offscreen (E-229; the game refuses that anyway).
    # A run during which the screen was locked rendered no or too few frames: it is repeated (at most
    # twice) once the screen is unlocked again.
    attempt=1
    while :; do
        wait_for_screen || return 1
        # shellcheck disable=SC2086
        env QTCRAFT_OFFSCREEN=0 QTCRAFT_TEXTURE_PACKS="${TEXTURE_PACKS:-none}" QTCRAFT_BENCHMARK_PACK_SWITCH="${PACK_SWITCH:-}" QTCRAFT_PREVIEW_ACTIVE="${PREVIEW_ACTIVE:-0}" QSG_RHI_PROFILE=1 QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$data" QTCRAFT_BENCHMARK="$seconds" QTCRAFT_BENCHMARK_SCENARIO="$walk" \
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
