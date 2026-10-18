# Creatures

[Back to the contents](index.md)

**QtCraft:** [Caped mannequin](#mob-qtcraft-caped_mannequin) · [Cave spider](#mob-qtcraft-cave_spider) · [Chicken](#mob-qtcraft-chicken) · [Enderman](#mob-qtcraft-enderman) · [Firefly](#mob-qtcraft-firefly) · [Iron golem](#mob-qtcraft-iron_golem) · [Mannequin](#mob-qtcraft-mannequin) · [Pig](#mob-qtcraft-pig) · [Sheep](#mob-qtcraft-sheep) · [Silverfish](#mob-qtcraft-silverfish) · [Skeleton](#mob-qtcraft-skeleton) · [Slime](#mob-qtcraft-slime) · [Spider](#mob-qtcraft-spider) · [Villager](#mob-qtcraft-villager) · [Zombie](#mob-qtcraft-zombie)

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

### <a id="mob-qtcraft-cave_spider"></a>Cave spider

A small, blue spider of caves and, later, mineshafts. It climbs walls like a spider, fits through gaps one block high and poisons you with its bite. Always hostile.

| | |
|---|---|
| ID | `qtcraft:cave_spider` |
| Behaviour | hostile: chases and attacks players |
| Health | 12 health points |
| Size | 0.7 m wide, 0.5 m tall |
| Speed | 1.6 m/s walking, 3.8 m/s chasing |
| Attack | 2 damage within 1.2 m, every 1 s |
| Sees players within | 16 m |
| Appears | at night |
| Caves | also in caves at any time |
| Spawn light | block light up to 7 |
| Spawn height | up to y 40 |
| Group size | up to 1 |
| Spawn weight | 0.4 |
| Limit | all hostile creatures together: 8 per player |
| Drops | [String](items.md#item-qtcraft-string), [Spider eye](items.md#item-qtcraft-spider_eye) (33 %) |

**Appears on**

[Stone](blocks.md#block-qtcraft-stone)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-chicken"></a>Chicken

A small bird of the meadows. It flutters down gently instead of falling and lays an egg every five to ten minutes. Seeds of any kind make it fall in love. Drops feathers and raw chicken.

| | |
|---|---|
| ID | `qtcraft:chicken` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 4 health points |
| Size | 0.4 m wide, 0.7 m tall |
| Speed | 1 m/s walking, 2.5 m/s fleeing |
| Appears | by day |
| Stays | never despawns and is saved with the world |
| Group size | up to 4 |
| Spawn weight | 2 |
| Limit | all peaceful creatures together: 6 per player |
| Breeds when fed | [Wheat seeds](items.md#item-qtcraft-wheat_seeds), [Melon seeds](items.md#item-qtcraft-melon_seeds), [Pumpkin seeds](items.md#item-qtcraft-pumpkin_seeds) |
| Breeding | again after 5 min, young grow up in 20 min |
| Lays | every 5–10 min: [Egg](items.md#item-qtcraft-egg) |
| Drops | [Feather](items.md#item-qtcraft-feather) (50 %), [Feather](items.md#item-qtcraft-feather) (50 %), [Raw chicken](food.md#item-qtcraft-raw_chicken) |

**Appears on**

[Grass block](blocks.md#block-qtcraft-grass_block)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-enderman"></a>Enderman

A tall, dark wanderer of the night. It leaves you alone until you look straight at its head or hurt it; then it chases you and hits hard. It teleports away when an arrow or a snowball hits it (they do not hurt it), and water and rain hurt it, so it keeps out of water and jumps away from it. Now and then it picks up a block such as grass, dirt or sand, carries it around in its hand and puts it down somewhere else; it drops it when it dies. Sometimes drops an ender pearl.

| | |
|---|---|
| ID | `qtcraft:enderman` |
| Behaviour | neutral: leaves players alone until one looks at its head or hurts it, then chases and attacks them |
| Health | 40 health points |
| Size | 0.6 m wide, 2.9 m tall |
| Speed | 1.5 m/s walking, 4 m/s chasing |
| Attack | 7 damage within 2 m, every 1 s |
| Sees players within | 32 m |
| Appears | at night |
| Spawn light | block light up to 7 |
| Group size | up to 1 |
| Spawn weight | 0.4 |
| Limit | all hostile creatures together: 8 per player |
| Drops | [Ender Pearl](items.md#item-qtcraft-ender_pearl) (50 %) |

**Appears on**

[Dirt](blocks.md#block-qtcraft-dirt), [Grass block](blocks.md#block-qtcraft-grass_block), [Sand](blocks.md#block-qtcraft-sand), [Snow block](blocks.md#block-qtcraft-snow_block), [Stone](blocks.md#block-qtcraft-stone)

**Appears in biomes**

All biomes.

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

### <a id="mob-qtcraft-iron_golem"></a>Iron golem

A big, slow guardian of iron. Villagers summon it when they panic or gossip, or you build it from four iron blocks and a carved pumpkin. It hunts monsters and throws them upwards; it is not pushed back by blows. Use an iron ingot on it to repair it.

| | |
|---|---|
| ID | `qtcraft:iron_golem` |
| Behaviour | guardian: hunts monsters, attacks players only when provoked |
| Health | 100 health points |
| Size | 1.4 m wide, 2.7 m tall |
| Speed | 0.9 m/s walking, 2.4 m/s chasing |
| Attack | 15 damage within 2.3 m, every 1.25 s |
| Sees players within | 16 m |
| Appears | never by itself (/summon, showcase world) |
| Drops | [Iron ingot](items.md#item-qtcraft-iron_ingot) (3×), [Iron ingot](items.md#item-qtcraft-iron_ingot) (50 %), [Iron ingot](items.md#item-qtcraft-iron_ingot) (50 %), [Poppy](blocks.md#block-qtcraft-poppy) (50 %), [Poppy](blocks.md#block-qtcraft-poppy) (50 %) |

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
| Breeds when fed | [Carrot](food.md#item-qtcraft-carrot), [Potato](food.md#item-qtcraft-potato) |
| Breeding | again after 5 min, young grow up in 20 min |
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
| Breeds when fed | [Wheat](items.md#item-qtcraft-wheat) |
| Breeding | again after 5 min, young grow up in 20 min |
| Drops | [White wool](blocks.md#block-qtcraft-white_wool), [White wool](blocks.md#block-qtcraft-white_wool) (50 %), [Raw mutton](food.md#item-qtcraft-raw_mutton), [Raw mutton](food.md#item-qtcraft-raw_mutton) (50 %) |

**Appears on**

[Grass block](blocks.md#block-qtcraft-grass_block)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-silverfish"></a>Silverfish

A small grey pest that lives inside stone. It comes out when you break an infested block, bites, and when you hurt it, it calls its kin out of the infested blocks around. Left alone, it slips back into a stone block next to it.

| | |
|---|---|
| ID | `qtcraft:silverfish` |
| Behaviour | hostile: chases and attacks players |
| Health | 8 health points |
| Size | 0.4 m wide, 0.3 m tall |
| Speed | 1.2 m/s walking, 3.5 m/s chasing |
| Attack | 1 damage within 1 m, every 1 s |
| Sees players within | 12 m |
| Appears | never by itself (/summon, showcase world) |
| Drops | nothing |

### <a id="mob-qtcraft-skeleton"></a>Skeleton

Comes out at night and lives in caves. Keeps its distance, circles you and shoots arrows with its bow once it has drawn it; it backs away when you come close. Its arrows cannot be picked up. Burns in direct sunlight.

| | |
|---|---|
| ID | `qtcraft:skeleton` |
| Behaviour | hostile: keeps its distance, circles and shoots at players |
| Health | 20 health points |
| Size | 0.6 m wide, 1.95 m tall |
| Speed | 1.4 m/s walking, 2.6 m/s chasing |
| Attack | 2 damage within 1.6 m, every 1 s |
| Sees players within | 16 m |
| Appears | at night |
| Caves | also in caves at any time |
| Spawn light | block light up to 7 |
| Group size | up to 2 |
| Spawn weight | 2 |
| Limit | all hostile creatures together: 8 per player |
| Drops | [Bone](items.md#item-qtcraft-bone), [Bone](items.md#item-qtcraft-bone) (50 %), [Arrow](items.md#item-qtcraft-arrow), [Arrow](items.md#item-qtcraft-arrow) (50 %) |

**Appears on**

[Dirt](blocks.md#block-qtcraft-dirt), [Grass block](blocks.md#block-qtcraft-grass_block), [Sand](blocks.md#block-qtcraft-sand), [Snow block](blocks.md#block-qtcraft-snow_block), [Stone](blocks.md#block-qtcraft-stone)

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

### <a id="mob-qtcraft-spider"></a>Spider

Climbs straight up walls and down again and leaps at its prey. By day in the open it leaves you alone unless you hit it; at night and in caves it hunts you. Wide: it does not fit through narrow gaps. Poison does nothing to it.

| | |
|---|---|
| ID | `qtcraft:spider` |
| Behaviour | hostile: chases and attacks players |
| Health | 16 health points |
| Size | 1.3 m wide, 0.9 m tall |
| Speed | 1.5 m/s walking, 3.6 m/s chasing |
| Attack | 2 damage within 1.7 m, every 1 s |
| Sees players within | 16 m |
| Appears | at night |
| Caves | also in caves at any time |
| Spawn light | block light up to 7 |
| Group size | up to 1 |
| Spawn weight | 2 |
| Limit | all hostile creatures together: 8 per player |
| Drops | [String](items.md#item-qtcraft-string), [String](items.md#item-qtcraft-string) (50 %), [Spider eye](items.md#item-qtcraft-spider_eye) (33 %) |

**Appears on**

[Dirt](blocks.md#block-qtcraft-dirt), [Grass block](blocks.md#block-qtcraft-grass_block), [Sand](blocks.md#block-qtcraft-sand), [Snow block](blocks.md#block-qtcraft-snow_block), [Stone](blocks.md#block-qtcraft-stone)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-villager"></a>Villager

Lives in a village with a bed of its own and, with luck, a workstation that gives it its profession. It works by day, meets the others at the bell, goes home in the evening and sleeps in its bed at night. It opens wooden doors and shuts them behind it, runs home from monsters and never fights.

| | |
|---|---|
| ID | `qtcraft:villager` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 20 health points |
| Size | 0.6 m wide, 1.9 m tall |
| Speed | 1.4 m/s walking, 3 m/s fleeing |
| Appears | never by itself (/summon, showcase world) |
| Drops | nothing |

### <a id="mob-qtcraft-zombie"></a>Zombie

Comes out at night and lives in caves. Chases players it sees and hits hard. Burns in direct sunlight.

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
| Drops | [Rotten flesh](food.md#item-qtcraft-rotten_flesh), [Carrot](food.md#item-qtcraft-carrot) (2.5 %), [Potato](food.md#item-qtcraft-potato) (2.5 %) |

**Appears on**

[Dirt](blocks.md#block-qtcraft-dirt), [Grass block](blocks.md#block-qtcraft-grass_block), [Sand](blocks.md#block-qtcraft-sand), [Snow block](blocks.md#block-qtcraft-snow_block), [Stone](blocks.md#block-qtcraft-stone)

**Appears in biomes**

All biomes.
