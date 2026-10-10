# Nahrung

[Zurück zum Inhalt](index.md)

**QtCraft:** [Apfel](#item-qtcraft-apple) · [Gebratenes Schweinefleisch](#item-qtcraft-cooked_porkchop) · [Pilzsuppe](#item-qtcraft-mushroom_stew) · [Rohes Schweinefleisch](#item-qtcraft-raw_porkchop) · [Verrottetes Fleisch](#item-qtcraft-rotten_flesh)

## QtCraft

### <a id="item-qtcraft-apple"></a>Apfel

Eichenlaub lässt hin und wieder einen fallen.

| | |
|---|---|
| ID | `qtcraft:apple` |
| Stapelgröße | 64 |
| Nahrung | 4 Nahrungspunkte |

**Droppt aus**

[Eichenlaub](blocks.md#block-qtcraft-oak_leaves) (2 %)

### <a id="item-qtcraft-cooked_porkchop"></a>Gebratenes Schweinefleisch

Gutes Essen, das die Nahrungsleiste weit füllt.

| | |
|---|---|
| ID | `qtcraft:cooked_porkchop` |
| Stapelgröße | 64 |
| Nahrung | 8 Nahrungspunkte |

**Im Ofen hergestellt**

*Ofen: 10 s pro Stück, braucht Brennstoff*

| 1 | → |
|---|---|
| [Rohes Schweinefleisch](#item-qtcraft-raw_porkchop) | [Gebratenes Schweinefleisch](#item-qtcraft-cooked_porkchop) |

[Ofen](blocks.md#block-qtcraft-furnace)

[Brennstoffe](tables.md#table-fuels)

### <a id="item-qtcraft-mushroom_stew"></a>Pilzsuppe

Eine sättigende Suppe aus beiden Pilzen; die Schüssel bleibt.

| | |
|---|---|
| ID | `qtcraft:mushroom_stew` |
| Stapelgröße | 1 |
| Nahrung | 6 Nahrungspunkte |
| Danach | [Schüssel](items.md#item-qtcraft-bowl) |

**Herstellung**

*Handwerksraster, beliebige Anordnung*

| 1 | 2 | → |
|---|---|---|
| [Brauner Pilz](blocks.md#block-qtcraft-brown_mushroom) | [Roter Pilz](blocks.md#block-qtcraft-red_mushroom) | [Pilzsuppe](#item-qtcraft-mushroom_stew) |
| [Schüssel](items.md#item-qtcraft-bowl) | · |  |

### <a id="item-qtcraft-raw_porkchop"></a>Rohes Schweinefleisch

Nahrung vom Schwein; im Ofen gebraten sättigt es mehr.

| | |
|---|---|
| ID | `qtcraft:raw_porkchop` |
| Stapelgröße | 64 |
| Nahrung | 3 Nahrungspunkte |

**Wird im Ofen zu**

*Ofen: 10 s pro Stück, braucht Brennstoff*

| 1 | → |
|---|---|
| [Rohes Schweinefleisch](#item-qtcraft-raw_porkchop) | [Gebratenes Schweinefleisch](#item-qtcraft-cooked_porkchop) |

[Ofen](blocks.md#block-qtcraft-furnace)

[Brennstoffe](tables.md#table-fuels)

**Droppt aus**

[Schwein](creatures.md#mob-qtcraft-pig) (2×)

### <a id="item-qtcraft-rotten_flesh"></a>Verrottetes Fleisch

Fällt von Zombies. Kaum Nahrung.

| | |
|---|---|
| ID | `qtcraft:rotten_flesh` |
| Stapelgröße | 64 |
| Nahrung | 2 Nahrungspunkte |

**Wird im Ofen zu**

*Ofen: 15 s pro Stück, braucht Brennstoff*

| 1 | → |
|---|---|
| [Verrottetes Fleisch](#item-qtcraft-rotten_flesh) | [Lederfetzen](items.md#item-qtcraft-leather_scrap) |

[Ofen](blocks.md#block-qtcraft-furnace)

[Brennstoffe](tables.md#table-fuels)

**Droppt aus**

[Zombie](creatures.md#mob-qtcraft-zombie)
