# Flüssigkeiten

[Zurück zum Inhalt](index.md)

**QtCraft:** [Lava](#block-qtcraft-lava) · [Wasser](#block-qtcraft-water)

## QtCraft

### <a id="block-qtcraft-lava"></a>Lava

Heiße Flüssigkeit, die leuchtet, Items verbrennt und jeden in Brand setzt, der sie berührt. Neben Wasser wird sie zu Obsidian oder Bruchstein; neben brennbaren Blöcken kann sie ein Feuer entfachen.

| | |
|---|---|
| ID | `qtcraft:lava` |
| Fließt | 3 Blöcke |
| Fließschritt alle | 1,5 s |
| Zwei Quellen bilden eine neue | nein |
| Schwimmgeschwindigkeit | 45 % |
| Eimer | [Lavaeimer](items.md#item-qtcraft-lava_bucket) |
| Wasser daneben | [Obsidian](blocks.md#block-qtcraft-obsidian) (Quelle), [Bruchstein](blocks.md#block-qtcraft-cobblestone) (fließend) |
| Licht | 15 von 15 |
| Lichtdämpfung | 2 von 15 |
| Fest | nein |
| Wird beim Bauen ersetzt | ja |
| Schaden bei Berührung | 3 Lebenspunkte |
| Setzt Körper in Brand für | 15 s |
| Entzündet Feuer daneben | 3 von 100 je Block-Update, neben brennbaren Blöcken |
| Zustände | falling (false, true); level (0, 1, 2, 3) |

**Kommt vor in**

[Lavabecken](worldgen.md#feature-qtcraft-lava_pool) (y 12–44), [Lavasee](worldgen.md#feature-qtcraft-lava_lake) (y 8–40)

### <a id="block-qtcraft-water"></a>Wasser

Fließt bis zu 7 Blöcke von einer Quelle; zwei Quellen bilden eine neue. Mit einem Eimer aufnehmbar.

| | |
|---|---|
| ID | `qtcraft:water` |
| Fließt | 7 Blöcke |
| Fließschritt alle | 0,25 s |
| Zwei Quellen bilden eine neue | ja |
| Eimer | [Wassereimer](items.md#item-qtcraft-water_bucket) |
| Lichtdämpfung | 2 von 15 |
| Fest | nein |
| Wird beim Bauen ersetzt | ja |
| Zustände | falling (false, true); level (0, 1, 2, 3, 4, 5, 6, 7) |
