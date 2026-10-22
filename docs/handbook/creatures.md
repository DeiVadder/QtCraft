# Creatures

[Back to the contents](index.md)

**QtCraft:** [Caped mannequin](#mob-qtcraft-caped_mannequin) · [Cat](#mob-qtcraft-cat) · [Cave spider](#mob-qtcraft-cave_spider) · [Chicken](#mob-qtcraft-chicken) · [Cod](#mob-qtcraft-cod) · [Cow](#mob-qtcraft-cow) · [Dog](#mob-qtcraft-dog) · [Enderman](#mob-qtcraft-enderman) · [Firefly](#mob-qtcraft-firefly) · [Iron golem](#mob-qtcraft-iron_golem) · [Mannequin](#mob-qtcraft-mannequin) · [Ocelot](#mob-qtcraft-ocelot) · [Pig](#mob-qtcraft-pig) · [Salmon](#mob-qtcraft-salmon) · [Sand golem](#mob-qtcraft-sand_golem) · [Sheep](#mob-qtcraft-sheep) · [Silverfish](#mob-qtcraft-silverfish) · [Skeleton](#mob-qtcraft-skeleton) · [Slime](#mob-qtcraft-slime) · [Spider](#mob-qtcraft-spider) · [Villager](#mob-qtcraft-villager) · [Witch](#mob-qtcraft-witch) · [Wolf](#mob-qtcraft-wolf) · [Zombie](#mob-qtcraft-zombie)

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

### <a id="mob-qtcraft-cat"></a>Cat

A cat of the villages, a few in every village. Feed it raw cod: each fish tames it with a chance of one in three. A tame cat follows you, sits when you use it and teleports to you like a dog, but does not fight. Creatures that are scared of cats keep away from it.

| | |
|---|---|
| ID | `qtcraft:cat` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 10 health points |
| Size | 0.6 m wide, 0.7 m tall |
| Speed | 1.4 m/s walking, 4 m/s fleeing |
| Appears | never by itself, but lives in villages (also /summon, showcase world) |
| Drops | [String](items.md#item-qtcraft-string) (50 %) |
| Tamed with | 33.33 % per item: [Raw cod](food.md#item-qtcraft-raw_cod) |
| As a pet | follows its owner, sits when its owner uses it and teleports to its owner from afar |
| Healed by | [Raw cod](food.md#item-qtcraft-raw_cod), [Cooked cod](food.md#item-qtcraft-cooked_cod) |

**Lives in villages of**

And in villages of biomes without a village style of their own.

[Desert](biomes.md#biome-qtcraft-desert), [Forest Steppe](biomes.md#biome-qtcraft-forest_steppe), [Plains](biomes.md#biome-qtcraft-plains), [Snowy Taiga](biomes.md#biome-qtcraft-snowy_taiga), [Swamp](biomes.md#biome-qtcraft-swamp), [Taiga](biomes.md#biome-qtcraft-taiga), [Tundra](biomes.md#biome-qtcraft-tundra)

### <a id="mob-qtcraft-cave_spider"></a>Cave spider

A small, blue spider of caves and mine shafts, where spawners bring it forth. It climbs walls like a spider, fits through gaps one block high and poisons you with its bite. Always hostile.

| | |
|---|---|
| ID | `qtcraft:cave_spider` |
| Behaviour | hostile: chases and attacks players |
| Health | 12 health points |
| Size | 0.7 m wide, 0.5 m tall |
| Speed | 1.6 m/s walking, 3.8 m/s chasing |
| Attack | 2 damage within 1.2 m, every 1 s |
| Sees players within | 16 m |
| Appears | day and night |
| Darkness | only in the dark: no block light, and a dark sky (at night, under roofs, in caves) |
| Caves | also in caves at any time |
| Spawn light | block light up to 0 |
| Spawn height | up to y 40 |
| Group size | up to 1 |
| Spawn weight | 0.4 |
| Limit | all monsters together: 70 per 289 loaded chunks around the players |
| Drops | [String](items.md#item-qtcraft-string), [Spider eye](items.md#item-qtcraft-spider_eye) (33 %) |

**Appears on**

Any solid, opaque block with a full top (no glass, leaves, ice or lower slab).

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

### <a id="mob-qtcraft-cod"></a>Cod

A fish of any water, lakes, rivers and seas alike. It swims in schools, flops about on land and suffocates there after a while. Catch it alive with a water bucket.

| | |
|---|---|
| ID | `qtcraft:cod` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 3 health points |
| Size | 0.5 m wide, 0.3 m tall |
| Speed | 1.5 m/s walking, 4 m/s fleeing |
| Appears | day and night |
| Stays | never despawns and is saved with the world |
| Group size | up to 4 |
| Spawn weight | 3 |
| Limit | all peaceful creatures together: 6 per player |
| Can be caught | [Water bucket](items.md#item-qtcraft-water_bucket), [Bucket of cod](items.md#item-qtcraft-cod_bucket) |
| Drops | [Raw cod](food.md#item-qtcraft-raw_cod), [Bone meal](items.md#item-qtcraft-bone_meal) (5 %) |

**Appears on**

[Water](liquids.md#block-qtcraft-water)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-cow"></a>Cow

A calm grazer of the meadows. Use an empty bucket on a grown cow to milk it; wheat makes it fall in love. Drops leather and raw beef.

| | |
|---|---|
| ID | `qtcraft:cow` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 10 health points |
| Size | 0.9 m wide, 1.4 m tall |
| Speed | 1 m/s walking, 2.5 m/s fleeing |
| Appears | by day |
| Stays | never despawns and is saved with the world |
| Group size | up to 4 |
| Spawn weight | 2 |
| Limit | all peaceful creatures together: 6 per player |
| Breeds when fed | [Wheat](items.md#item-qtcraft-wheat) |
| Breeding | again after 5 min, young grow up in 20 min |
| Drops | [Leather](items.md#item-qtcraft-leather), [Leather](items.md#item-qtcraft-leather) (50 %), [Raw beef](food.md#item-qtcraft-raw_beef), [Raw beef](food.md#item-qtcraft-raw_beef), [Raw beef](food.md#item-qtcraft-raw_beef) (50 %) |

**Appears on**

[Grass block](blocks.md#block-qtcraft-grass_block)

**Appears in biomes**

All biomes.

### <a id="mob-qtcraft-dog"></a>Dog

A tamed wolf with a red collar. It belongs to you, also when you are offline: it follows you, teleports to you when you are far away (only to a safe place in loaded chunks) and fights what hurts you and what you hit, never you or your other pets. Use it to make it sit or stand up; meat heals it, a dye colours its collar.

| | |
|---|---|
| ID | `qtcraft:dog` |
| Behaviour | neutral: leaves players alone until one hurts it, then chases and attacks them |
| Health | 20 health points |
| Size | 0.6 m wide, 0.85 m tall |
| Speed | 1.6 m/s walking, 4.5 m/s fleeing |
| Appears | never by itself (/summon, showcase world) |
| Drops | nothing |
| Tamed with | 100 % per item: [Bone](items.md#item-qtcraft-bone) |
| As a pet | follows its owner, sits when its owner uses it, teleports to its owner from afar and fights what hurts its owner or what its owner hits |
| Healed by | [Raw porkchop](food.md#item-qtcraft-raw_porkchop), [Cooked porkchop](food.md#item-qtcraft-cooked_porkchop), [Raw mutton](food.md#item-qtcraft-raw_mutton), [Cooked mutton](food.md#item-qtcraft-cooked_mutton), [Rotten flesh](food.md#item-qtcraft-rotten_flesh) |
| Collar | the colour of this dye at first; a dye used on it by its owner colours it: [Red dye](items.md#item-qtcraft-red_dye) |

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
| Appears | day and night |
| Darkness | only in the dark: no block light, and a dark sky (at night, under roofs, in caves) |
| Spawn light | block light up to 0 |
| Group size | up to 1 |
| Spawn weight | 0.4 |
| Limit | all monsters together: 70 per 289 loaded chunks around the players |
| Drops | [Ender Pearl](items.md#item-qtcraft-ender_pearl) (50 %) |

**Appears on**

Any solid, opaque block with a full top (no glass, leaves, ice or lower slab).

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

Any solid block, glass and leaves too, or the surface of a liquid.

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

### <a id="mob-qtcraft-ocelot"></a>Ocelot

A shy spotted cat of the jungle (until there is one, of swamps). It runs away from anyone who comes close, unless you hold raw cod and sneak up slowly. It cannot be tamed; two fed with raw cod get a young one.

| | |
|---|---|
| ID | `qtcraft:ocelot` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 10 health points |
| Size | 0.6 m wide, 0.7 m tall |
| Speed | 1.6 m/s walking, 5 m/s fleeing |
| Appears | by day |
| Stays | never despawns and is saved with the world |
| Group size | up to 2 |
| Spawn weight | 0.6 |
| Limit | all peaceful creatures together: 6 per player |
| Breeds when fed | [Raw cod](food.md#item-qtcraft-raw_cod) |
| Breeding | again after 5 min, young grow up in 20 min |
| Drops | nothing |
| Shy | runs from players closer than 8 m, unless they hold this and move slowly: [Raw cod](food.md#item-qtcraft-raw_cod) |

**Appears on**

[Grass block](blocks.md#block-qtcraft-grass_block)

**Appears in biomes**

[Jungle](biomes.md#biome-qtcraft-jungle), [Jungle Edge](biomes.md#biome-qtcraft-jungle_edge), [Swamp](biomes.md#biome-qtcraft-swamp)

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

**Lives in villages of**

And in villages of biomes without a village style of their own.

[Desert](biomes.md#biome-qtcraft-desert), [Forest Steppe](biomes.md#biome-qtcraft-forest_steppe), [Plains](biomes.md#biome-qtcraft-plains), [Snowy Taiga](biomes.md#biome-qtcraft-snowy_taiga), [Taiga](biomes.md#biome-qtcraft-taiga), [Tundra](biomes.md#biome-qtcraft-tundra)

### <a id="mob-qtcraft-salmon"></a>Salmon

A fish of the cold waters of taiga, tundra and mountains. It swims in schools, flops about on land and suffocates there after a while. Catch it alive with a water bucket.

| | |
|---|---|
| ID | `qtcraft:salmon` |
| Behaviour | peaceful: wanders, flees when hurt |
| Health | 3 health points |
| Size | 0.5 m wide, 0.35 m tall |
| Speed | 1.5 m/s walking, 4 m/s fleeing |
| Appears | day and night |
| Stays | never despawns and is saved with the world |
| Group size | up to 4 |
| Spawn weight | 3 |
| Limit | all peaceful creatures together: 6 per player |
| Can be caught | [Water bucket](items.md#item-qtcraft-water_bucket), [Bucket of salmon](items.md#item-qtcraft-salmon_bucket) |
| Drops | [Raw salmon](food.md#item-qtcraft-raw_salmon), [Bone meal](items.md#item-qtcraft-bone_meal) (5 %) |

**Appears on**

[Water](liquids.md#block-qtcraft-water)

**Appears in biomes**

[Mountains](biomes.md#biome-qtcraft-mountains), [Snowy Taiga](biomes.md#biome-qtcraft-snowy_taiga), [Taiga](biomes.md#biome-qtcraft-taiga), [Tundra](biomes.md#biome-qtcraft-tundra)

### <a id="mob-qtcraft-sand_golem"></a>Sand golem

A golem of sand that guards the cellar of a desert temple and attacks whoever climbs down to its chests; it does not follow far. It is found nowhere else and cannot be built. Where it touches a water source block it crumbles into four blocks of sand in the shape of its build, with carved sandstone as its head; its arms fall as sand and can fill a water hole. In a dark cellar with no player close by, a new one comes back after a while: a renewable source of sand.

| | |
|---|---|
| ID | `qtcraft:sand_golem` |
| Behaviour | guardian: keeps to the place it guards and attacks players who come close |
| Health | 60 health points |
| Size | 1.4 m wide, 2.7 m tall |
| Speed | 0.8 m/s walking, 2.2 m/s chasing |
| Attack | 9 damage within 2.3 m, every 1.5 s |
| Sees players within | 14 m |
| Appears | never by itself (/summon, showcase world) |
| Drops | [Sand](blocks.md#block-qtcraft-sand) (2×), [Sand](blocks.md#block-qtcraft-sand) (50 %), [Gold nugget](items.md#item-qtcraft-gold_nugget) (30 %) |

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
| Appears | day and night |
| Darkness | only in the dark: no block light, and a dark sky (at night, under roofs, in caves) |
| Caves | also in caves at any time |
| Spawn light | block light up to 0 |
| Group size | up to 2 |
| Spawn weight | 2 |
| Limit | all monsters together: 70 per 289 loaded chunks around the players |
| Drops | [Bone](items.md#item-qtcraft-bone), [Bone](items.md#item-qtcraft-bone) (50 %), [Arrow](items.md#item-qtcraft-arrow), [Arrow](items.md#item-qtcraft-arrow) (50 %) |

**Appears on**

Any solid, opaque block with a full top (no glass, leaves, ice or lower slab).

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
| Spawn height | up to y 40 |
| Spawn chunks | only in 10 % of the chunks, fixed by the world's seed |
| Group size | up to 2 |
| Spawn weight | 4 |
| Limit | all monsters together: 70 per 289 loaded chunks around the players |
| Drops | [Slimeball](items.md#item-qtcraft-slimeball) |

**Appears on**

Any solid, opaque block with a full top (no glass, leaves, ice or lower slab).

**Appears in biomes**

All biomes.

**Also appears**

at night; only in the dark: no block light, and a dark sky (at night, under roofs, in caves); up to y 70; block light up to 0

[Swamp](biomes.md#biome-qtcraft-swamp)

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
| Appears | day and night |
| Darkness | only in the dark: no block light, and a dark sky (at night, under roofs, in caves) |
| Caves | also in caves at any time |
| Spawn light | block light up to 0 |
| Group size | up to 1 |
| Spawn weight | 2 |
| Limit | all monsters together: 70 per 289 loaded chunks around the players |
| Drops | [String](items.md#item-qtcraft-string), [String](items.md#item-qtcraft-string) (50 %), [Spider eye](items.md#item-qtcraft-spider_eye) (33 %) |

**Appears on**

Any solid, opaque block with a full top (no glass, leaves, ice or lower slab).

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

### <a id="mob-qtcraft-witch"></a>Witch

Lives in a hut on stilts in the swamp and comes back there while no player is near. She keeps her distance and throws splash potions: poison while you are not poisoned, harming once you are. When hurt she stops to drink a potion of healing. She is immune to poison.

| | |
|---|---|
| ID | `qtcraft:witch` |
| Behaviour | hostile: keeps its distance, circles and shoots at players |
| Health | 26 health points |
| Size | 0.6 m wide, 1.95 m tall |
| Speed | 1.2 m/s walking, 2.4 m/s chasing |
| Sees players within | 16 m |
| Appears | never by itself (/summon, showcase world) |
| Drops | [Glass bottle](items.md#item-qtcraft-glass_bottle) (50 %), [Sugar](items.md#item-qtcraft-sugar) (40 %), [Stick](items.md#item-qtcraft-stick) (40 %), [Spider eye](items.md#item-qtcraft-spider_eye) (30 %), [Signal Dust](blocks.md#block-qtcraft-signal_dust) (30 %) |

### <a id="mob-qtcraft-wolf"></a>Wolf

A grey hunter of forests, taiga and tundra that roams in packs. It leaves you alone until you hit one; then the whole pack nearby chases you. Feed it bones: each bone tames it with a chance of one in three, and it becomes your dog.

| | |
|---|---|
| ID | `qtcraft:wolf` |
| Behaviour | neutral: leaves players alone until one hurts it, then chases and attacks them |
| Health | 8 health points |
| Size | 0.6 m wide, 0.85 m tall |
| Speed | 1.6 m/s walking, 4 m/s fleeing |
| Appears | by day |
| Stays | never despawns and is saved with the world |
| Group size | up to 4 |
| Spawn weight | 1 |
| Limit | all peaceful creatures together: 6 per player |
| Drops | nothing |
| Tamed with | 33.33 % per item: [Bone](items.md#item-qtcraft-bone) |
| Becomes | [Dog](#mob-qtcraft-dog) |
| As a pet | follows its owner, sits when its owner uses it, teleports to its owner from afar and fights what hurts its owner or what its owner hits |
| Collar | the colour of this dye at first; a dye used on it by its owner colours it: [Red dye](items.md#item-qtcraft-red_dye) |
| Pack | strike one and the others within 16 m chase you too |

**Appears on**

[Grass block](blocks.md#block-qtcraft-grass_block), [Snow block](blocks.md#block-qtcraft-snow_block)

**Appears in biomes**

[Birch Forest](biomes.md#biome-qtcraft-birch_forest), [Forest](biomes.md#biome-qtcraft-forest), [Snowy Taiga](biomes.md#biome-qtcraft-snowy_taiga), [Taiga](biomes.md#biome-qtcraft-taiga), [Tundra](biomes.md#biome-qtcraft-tundra)

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
| Appears | day and night |
| Darkness | only in the dark: no block light, and a dark sky (at night, under roofs, in caves) |
| Caves | also in caves at any time |
| Spawn light | block light up to 0 |
| Group size | up to 3 |
| Spawn weight | 3 |
| Limit | all monsters together: 70 per 289 loaded chunks around the players |
| Drops | [Rotten flesh](food.md#item-qtcraft-rotten_flesh), [Carrot](food.md#item-qtcraft-carrot) (2.5 %), [Potato](food.md#item-qtcraft-potato) (2.5 %) |

**Appears on**

Any solid, opaque block with a full top (no glass, leaves, ice or lower slab).

**Appears in biomes**

All biomes.
