# Creatures

[Back to the contents](index.md)

**QtCraft:** [Caped mannequin](#mob-qtcraft-caped_mannequin) · [Firefly](#mob-qtcraft-firefly) · [Mannequin](#mob-qtcraft-mannequin) · [Pig](#mob-qtcraft-pig) · [Sheep](#mob-qtcraft-sheep) · [Slime](#mob-qtcraft-slime) · [Zombie](#mob-qtcraft-zombie)

## QtCraft

### <a id="mob-qtcraft-caped_mannequin"></a>Caped mannequin

A mannequin wearing the green cape. It walks about slowly, so the cape swings; it never appears by itself, the showcase world has one at the character stand.

| | |
|---|---|
| ID | `qtcraft:caped_mannequin` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 20 health points |
| Size | 0.6 m wide, 1.8 m tall |
| Speed | 1.4 m/s walking, 1.5 m/s fleeing |
| Appears | never by itself (/summon, showcase world) |
| Drops | nothing |

### <a id="mob-qtcraft-firefly"></a>Firefly

Glows over swamps at night. Catch it with an empty glass bottle.

| | |
|---|---|
| ID | `qtcraft:firefly` |
| Behaviour | harmless ambience |
| Health | 1 health point |
| Size | 0.25 m wide, 0.25 m tall |
| Speed | 0.8 m/s |
| Appears | at night |
| Group size | up to 3 |
| Spawn weight | 10 |
| Limit | 8 near each player; all ambient creatures together: 10 per player |
| Flies | yes |
| Glows | 15 of 15 |
| Can be caught | [Glass bottle](items.md#item-qtcraft-glass_bottle), [Firefly in a bottle](items.md#item-qtcraft-firefly_bottle) |
| Drops | nothing |

**Appears on**

Any solid block or the surface of a liquid.

**Appears in biomes**

[Swamp](biomes.md#biome-qtcraft-swamp)

### <a id="mob-qtcraft-mannequin"></a>Mannequin

A figure with the character model that walks about slowly. It never appears by itself; the showcase world has some, /summon makes more.

| | |
|---|---|
| ID | `qtcraft:mannequin` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 20 health points |
| Size | 0.6 m wide, 1.8 m tall |
| Speed | 0.9 m/s walking, 1.5 m/s fleeing |
| Appears | never by itself (/summon, showcase world) |
| Drops | nothing |

### <a id="mob-qtcraft-pig"></a>Pig

Peaceful farm animal of grassy land; drops porkchops.

| | |
|---|---|
| ID | `qtcraft:pig` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 10 health points |
| Size | 0.9 m wide, 0.9 m tall |
| Speed | 1 m/s walking, 2.5 m/s fleeing |
| Appears | by day |
| Stays | never despawns and is saved with the world |
| Group size | up to 4 |
| Spawn weight | 2 |
| Limit | all peaceful creatures together: 6 per player |
| Drops | [Raw porkchop](food.md#item-qtcraft-raw_porkchop) (2×) |

**Appears on**

[Grass block](blocks.md#block-qtcraft-grass_block)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-sheep"></a>Sheep

Peaceful animal of grassy land; drops wool.

| | |
|---|---|
| ID | `qtcraft:sheep` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 8 health points |
| Size | 0.9 m wide, 1.2 m tall |
| Speed | 1 m/s walking, 2.5 m/s fleeing |
| Appears | by day |
| Stays | never despawns and is saved with the world |
| Group size | up to 4 |
| Spawn weight | 2 |
| Limit | all peaceful creatures together: 6 per player |
| Drops | [White wool](blocks.md#block-qtcraft-white_wool), [White wool](blocks.md#block-qtcraft-white_wool) |

**Appears on**

[Grass block](blocks.md#block-qtcraft-grass_block)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-slime"></a>Slime

A bouncing blob, weak but persistent. Underground it lives deep down and only in some chunks, the slime chunks; at night it also comes out in swamps.

| | |
|---|---|
| ID | `qtcraft:slime` |
| Behaviour | hostile: chases and attacks players |
| Health | 8 health points |
| Size | 0.8 m wide, 0.8 m tall |
| Speed | 1 m/s walking, 2.4 m/s chasing |
| Attack | 2 damage within 1.3 m, every 1.2 s |
| Sees players within | 12 m |
| Appears | day and night |
| Caves | also in caves at any time |
| Spawn light | block light up to 7 |
| Spawn height | up to y 40 |
| Spawn chunks | only in 10 % of the chunks, fixed by the world's seed |
| Group size | up to 2 |
| Spawn weight | 4 |
| Limit | all hostile creatures together: 8 per player |
| Drops | [Slimeball](items.md#item-qtcraft-slimeball) |

**Appears on**

[Dirt](blocks.md#block-qtcraft-dirt), [Gravel](blocks.md#block-qtcraft-gravel), [Stone](blocks.md#block-qtcraft-stone)

**Appears in biomes**

All biomes.

**Also appears**

at night; up to y 70; block light up to 7

[Swamp](biomes.md#biome-qtcraft-swamp), [Grass block](blocks.md#block-qtcraft-grass_block), [Dirt](blocks.md#block-qtcraft-dirt)

### <a id="mob-qtcraft-zombie"></a>Zombie

Comes out at night and lives in caves. Chases players it sees and hits hard.

| | |
|---|---|
| ID | `qtcraft:zombie` |
| Behaviour | hostile: chases and attacks players |
| Health | 20 health points |
| Size | 0.6 m wide, 1.95 m tall |
| Speed | 1.2 m/s walking, 3.2 m/s chasing |
| Attack | 3 damage within 1.6 m, every 1 s |
| Sees players within | 20 m |
| Appears | at night |
| Caves | also in caves at any time |
| Spawn light | block light up to 7 |
| Group size | up to 3 |
| Spawn weight | 3 |
| Limit | all hostile creatures together: 8 per player |
| Drops | [Rotten flesh](food.md#item-qtcraft-rotten_flesh) |

**Appears on**

[Dirt](blocks.md#block-qtcraft-dirt), [Grass block](blocks.md#block-qtcraft-grass_block), [Sand](blocks.md#block-qtcraft-sand), [Snow block](blocks.md#block-qtcraft-snow_block), [Stone](blocks.md#block-qtcraft-stone)

**Appears in biomes**

All biomes.
