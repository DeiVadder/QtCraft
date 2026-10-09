# Liquids

[Back to the contents](index.md)

**QtCraft:** [Lava](#block-qtcraft-lava) · [Water](#block-qtcraft-water)

## QtCraft

### <a id="block-qtcraft-lava"></a>Lava

Hot liquid that glows, burns items and hurts. Next to water it turns into obsidian or cobblestone.

| | |
|---|---|
| ID | `qtcraft:lava` |
| Flows | 3 blocks |
| Flow step every | 1.5 s |
| Two sources make a new one | no |
| Swimming speed | 45 % |
| Bucket | [Lava bucket](items.md#item-qtcraft-lava_bucket) |
| Water nearby | [Obsidian](blocks.md#block-qtcraft-obsidian) (source), [Cobblestone](blocks.md#block-qtcraft-cobblestone) (flowing) |
| Light | 15 of 15 |
| Light blocked | 2 of 15 |
| Solid | no |
| Replaced when building into it | yes |
| Damage on contact | 3 health points |
| States | falling (false, true); level (0, 1, 2, 3) |

**Found in**

[Lava pool](worldgen.md#feature-qtcraft-lava_pool) (y 5–40)

### <a id="block-qtcraft-water"></a>Water

Flows up to 7 blocks from a source; two sources make a new one. Take it with a bucket.

| | |
|---|---|
| ID | `qtcraft:water` |
| Flows | 7 blocks |
| Flow step every | 0.25 s |
| Two sources make a new one | yes |
| Bucket | [Water bucket](items.md#item-qtcraft-water_bucket) |
| Light blocked | 2 of 15 |
| Solid | no |
| Replaced when building into it | yes |
| States | falling (false, true); level (0, 1, 2, 3, 4, 5, 6, 7) |
