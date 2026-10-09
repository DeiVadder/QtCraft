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
#   inventory, furnace, handbook - standing with the inventory, a filled furnace (looking down at
#              it) or the handbook page of stone open (QML screens over the world)

limited() {
    perl -e 'alarm shift; exec @ARGV or die' "$@"
}

CROWD_ITEMS="coal diamond gold_ingot iron_ingot stick raw_porkchop rotten_flesh slimeball wooden_sword stone_sword
iron_sword diamond_sword wooden_pickaxe stone_pickaxe iron_pickaxe diamond_pickaxe wooden_axe stone_axe iron_axe
diamond_axe wooden_shovel stone_shovel iron_shovel diamond_shovel"

# Runs one benchmark of the game.
# Usage: run_game <app> <scenario> <seconds> <view distance> <data dir> <log> [wrapper command...]
# The wrapper (e.g. a profiler) is put in front of the app. Prints the BENCH line. The run is killed
# after RUN_LIMIT seconds (default: its length plus 120 s).
run_game() {
    app=$1
    scenario=$2
    seconds=$3
    view=$4
    data=$5
    log=$6
    shift 6
    wrapper="$*"
    look=45,-10
    walk=stand
    case "$scenario" in
    sprint | walk) walk=$scenario ;;
    light | liquids) walk=$scenario ;;
    furnace) look=0,-89 ;;
    esac
    set -- --play --seed 42 --view-distance "$view" --look "$look"
    case "$scenario" in
    light)
        set -- "$@" --chat "/time set night" --chat "/fill ~-11 ~-1 ~-11 ~11 ~-1 ~11 stone" --chat "/fill ~-11 ~ ~-11 ~11 ~4 ~11 air"
        ;;
    liquids)
        set -- "$@" --chat "/fill ~-18 ~-1 ~-18 ~18 ~-1 ~18 stone" --chat "/fill ~-18 ~ ~-18 ~18 ~6 ~18 air"
        ;;
    crowd)
        set -- "$@" --chat "/time set day" --chat "/summon qtcraft:pig 16" --chat "/summon qtcraft:pig 16" \
            --chat "/summon qtcraft:zombie 16" --chat "/summon qtcraft:slime 8"
        for item in $CROWD_ITEMS; do
            set -- "$@" --chat "/spawnitem qtcraft:$item 1"
        done
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
    esac
    # Sound is mixed as in a normal game but muted, so runs stay quiet. GPU timestamps
    # (QSG_RHI_PROFILE) give the GPU time per frame. The window opens on the built-in display
    # (QTCRAFT_SCREEN overrides it) and stays in the background: no focus, no captured mouse.
    mkdir -p "$data"
    printf '[audio]\nmuted=true\n' >"$data/settings.ini"
    # shellcheck disable=SC2086
    env QSG_RHI_PROFILE=1 QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$data" QTCRAFT_BENCHMARK="$seconds" QTCRAFT_BENCHMARK_SCENARIO="$walk" \
        perl -e 'alarm shift; exec @ARGV or die' "${RUN_LIMIT:-$((seconds + 120))}" $wrapper "$app" "$@" >"$log" 2>&1
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
