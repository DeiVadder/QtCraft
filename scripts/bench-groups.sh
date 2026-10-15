# Named groups of benchmark scenarios (sourced by bench-ab.sh, bench-headless.sh and bench.sh; no side
# effects). SCENARIOS="@core,@signal flat-crowd" picks scenarios by group and by name, separated by
# blanks or commas (decision E-388). Which group fits which kind of change: docs/Performance.md,
# "Szenario-Gruppen". The scenarios themselves: scripts/bench-common.sh.
#
# Groups of the window (scripts/bench-ab.sh; frame times, GPU, client CPU, the whole memory):
#   core       stand sprint showcase crowd: the default, for every stage
#   world      walk sprint flight flat-crowd: terrain generation, streaming, chunk arrival
#   light      light glass glass-full void-light: block light, translucency, remeshing
#   liquids    liquids flat-liquids liquid-fill: liquid meshing and flow
#   entities   crowd flat-crowd armour-crowd armour-walk: entity meshing, attachments, capes
#   signal     signal-stress showcase-signal_components showcase-signal_machines showcase-signal_farm
#   ui         inventory furnace handbook: QML screens over the world
#   memory     explore showcase-flight: memory and unloading over a flight (with STATS=5000)
# Groups of the headless measurement (scripts/bench-headless.sh; server tick, phases, server memory):
#   server     crowd gravel-collapse farm farm-seeds explore showcase-signal_components
#              showcase-signal_machines showcase-signal_farm: the default
#   farm       farm farm-seeds
#   fire       forest-fire: fire steps, burning blocks and light (stage 34.2; both scripts)
# Every group works in both scripts; the window groups also say which server scenes to measure
# headless for a change of that kind (e.g. @liquids headless for the flow steps).

# The scenarios of group $1 (without "@"); fails for an unknown group.
scenario_group() {
    case "$1" in
    core) echo "stand sprint showcase crowd" ;;
    world) echo "walk sprint flight flat-crowd" ;;
    light) echo "light glass glass-full void-light" ;;
    liquids) echo "liquids flat-liquids liquid-fill" ;;
    entities) echo "crowd flat-crowd armour-crowd armour-walk" ;;
    signal) echo "signal-stress showcase-signal_components showcase-signal_machines showcase-signal_farm" ;;
    ui) echo "inventory furnace handbook" ;;
    memory) echo "explore showcase-flight" ;;
    server) echo "crowd gravel-collapse farm farm-seeds explore showcase-signal_components showcase-signal_machines showcase-signal_farm" ;;
    farm) echo "farm farm-seeds" ;;
    fire) echo "forest-fire" ;;
    *) return 1 ;;
    esac
}

# The scenarios of a selection like "@core,@signal flat-crowd", groups expanded, each once in the
# order of first mention, separated by blanks. Prints an error and fails for an unknown group.
expand_scenarios() {
    expanded=
    for word in $(printf '%s\n' "$1" | tr ',' ' '); do
        case "$word" in
        @*)
            members=$(scenario_group "${word#@}") || {
                echo "unknown scenario group: $word (core, world, light, liquids, entities, signal, ui, memory, server, farm, fire)" >&2
                return 1
            }
            ;;
        *) members=$word ;;
        esac
        for scenario in $members; do
            case " $expanded " in
            *" $scenario "*) ;;
            *) expanded="$expanded${expanded:+ }$scenario" ;;
            esac
        done
    done
    echo "$expanded"
}
