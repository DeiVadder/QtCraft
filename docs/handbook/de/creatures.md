# Wesen

[Zurück zum Inhalt](index.md)

**QtCraft:** [Dorfbewohner](#mob-qtcraft-villager) · [Gliederpuppe](#mob-qtcraft-mannequin) · [Gliederpuppe mit Umhang](#mob-qtcraft-caped_mannequin) · [Glühwürmchen](#mob-qtcraft-firefly) · [Schaf](#mob-qtcraft-sheep) · [Schleim](#mob-qtcraft-slime) · [Schwein](#mob-qtcraft-pig) · [Zombie](#mob-qtcraft-zombie)

## QtCraft

### <a id="mob-qtcraft-villager"></a>Dorfbewohner

Lebt in einem Dorf mit eigenem Bett und, mit Glück, einem Arbeitsplatz, der ihm seinen Beruf gibt. Tagsüber arbeitet er, trifft die anderen an der Glocke, geht abends nach Hause und schläft nachts in seinem Bett. Er öffnet Holztüren und schließt sie hinter sich, flieht vor Monstern nach Hause und kämpft nie.

| | |
|---|---|
| ID | `qtcraft:villager` |
| Verhalten | friedlich: wandert, flieht bei Treffern |
| Gesundheit | 20 Lebenspunkte |
| Größe | 0,6 m breit, 1,9 m hoch |
| Geschwindigkeit | 1,4 m/s beim Gehen, 3 m/s auf der Flucht |
| Erscheint | nie von selbst (/summon, Showcase-Welt) |
| Beute | nichts |

### <a id="mob-qtcraft-mannequin"></a>Gliederpuppe

Eine Figur mit dem Charaktermodell, die langsam umhergeht. Sie erscheint nie von selbst; die Showcase-Welt hat einige, /summon erzeugt weitere.

| | |
|---|---|
| ID | `qtcraft:mannequin` |
| Verhalten | friedlich: wandert, flieht bei Treffern |
| Gesundheit | 20 Lebenspunkte |
| Größe | 0,6 m breit, 1,8 m hoch |
| Geschwindigkeit | 0,9 m/s beim Gehen, 1,5 m/s auf der Flucht |
| Erscheint | nie von selbst (/summon, Showcase-Welt) |
| Beute | nichts |

### <a id="mob-qtcraft-caped_mannequin"></a>Gliederpuppe mit Umhang

Eine Gliederpuppe mit dem grünen Umhang. Sie geht langsam umher, damit der Umhang schwingt; sie erscheint nie von selbst, die Showcase-Welt hat eine am Charakter-Stand.

| | |
|---|---|
| ID | `qtcraft:caped_mannequin` |
| Verhalten | friedlich: wandert, flieht bei Treffern |
| Gesundheit | 20 Lebenspunkte |
| Größe | 0,6 m breit, 1,8 m hoch |
| Geschwindigkeit | 1,4 m/s beim Gehen, 1,5 m/s auf der Flucht |
| Erscheint | nie von selbst (/summon, Showcase-Welt) |
| Beute | nichts |

### <a id="mob-qtcraft-firefly"></a>Glühwürmchen

Leuchtet nachts über Sümpfen. Mit einer leeren Glasflasche fangbar.

| | |
|---|---|
| ID | `qtcraft:firefly` |
| Verhalten | harmlose Umgebung |
| Gesundheit | 1 Lebenspunkt |
| Größe | 0,25 m breit, 0,25 m hoch |
| Geschwindigkeit | 0,8 m/s |
| Erscheint | nachts |
| Gruppengröße | bis 3 |
| Erscheinungsgewicht | 10 |
| Obergrenze | 8 bei jedem Spieler; alle Umgebungswesen zusammen: 10 pro Spieler |
| Fliegt | ja |
| Leuchtet | 15 von 15 |
| Fangbar | [Glasflasche](items.md#item-qtcraft-glass_bottle), [Glühwürmchen in der Flasche](items.md#item-qtcraft-firefly_bottle) |
| Beute | nichts |

**Erscheint auf**

Jeder feste Block oder eine Flüssigkeitsoberfläche.

**Erscheint in Biomen**

[Sumpf](biomes.md#biome-qtcraft-swamp)

### <a id="mob-qtcraft-sheep"></a>Schaf

Friedliches Tier des Graslands; lässt Wolle fallen.

| | |
|---|---|
| ID | `qtcraft:sheep` |
| Verhalten | friedlich: wandert, flieht bei Treffern |
| Gesundheit | 8 Lebenspunkte |
| Größe | 0,9 m breit, 1,2 m hoch |
| Geschwindigkeit | 1 m/s beim Gehen, 2,5 m/s auf der Flucht |
| Erscheint | bei Tag |
| Bleibt | verschwindet nie und wird mit der Welt gespeichert |
| Gruppengröße | bis 4 |
| Erscheinungsgewicht | 2 |
| Obergrenze | alle friedlichen Wesen zusammen: 6 pro Spieler |
| Vermehrt sich mit | [Weizen](items.md#item-qtcraft-wheat) |
| Nachwuchs | wieder nach 5 min, Junge wachsen in 20 min heran |
| Beute | [Weiße Wolle](blocks.md#block-qtcraft-white_wool), [Weiße Wolle](blocks.md#block-qtcraft-white_wool), [Rohes Hammelfleisch](food.md#item-qtcraft-raw_mutton), [Rohes Hammelfleisch](food.md#item-qtcraft-raw_mutton) |

**Erscheint auf**

[Grasblock](blocks.md#block-qtcraft-grass_block)

**Erscheint in Biomen**

Alle Biome.

### <a id="mob-qtcraft-slime"></a>Schleim

Ein hüpfender Klumpen, schwach, aber hartnäckig. Unter der Erde lebt er tief unten und nur in manchen Chunks, den Slime-Chunks; nachts kommt er auch in Sümpfen heraus.

| | |
|---|---|
| ID | `qtcraft:slime` |
| Verhalten | feindlich: verfolgt und greift Spieler an |
| Gesundheit | 8 Lebenspunkte |
| Größe | 0,8 m breit, 0,8 m hoch |
| Geschwindigkeit | 1 m/s beim Gehen, 2,4 m/s beim Verfolgen |
| Angriff | 2 Schaden bis 1,3 m, alle 1,2 s |
| Sieht Spieler bis | 12 m |
| Erscheint | Tag und Nacht |
| Höhlen | auch in Höhlen zu jeder Zeit |
| Licht beim Erscheinen | Blocklicht bis 7 |
| Spawnhöhe | bis y 40 |
| Spawn-Chunks | nur in 10 % der Chunks, festgelegt durch den Seed der Welt |
| Gruppengröße | bis 2 |
| Erscheinungsgewicht | 4 |
| Obergrenze | alle feindlichen Wesen zusammen: 8 pro Spieler |
| Beute | [Schleimball](items.md#item-qtcraft-slimeball) |

**Erscheint auf**

[Erde](blocks.md#block-qtcraft-dirt), [Kies](blocks.md#block-qtcraft-gravel), [Stein](blocks.md#block-qtcraft-stone)

**Erscheint in Biomen**

Alle Biome.

**Erscheint auch**

nachts; bis y 70; Blocklicht bis 7

[Sumpf](biomes.md#biome-qtcraft-swamp), [Grasblock](blocks.md#block-qtcraft-grass_block), [Erde](blocks.md#block-qtcraft-dirt)

### <a id="mob-qtcraft-pig"></a>Schwein

Friedliches Tier des Graslands; lässt Schweinefleisch fallen.

| | |
|---|---|
| ID | `qtcraft:pig` |
| Verhalten | friedlich: wandert, flieht bei Treffern |
| Gesundheit | 10 Lebenspunkte |
| Größe | 0,9 m breit, 0,9 m hoch |
| Geschwindigkeit | 1 m/s beim Gehen, 2,5 m/s auf der Flucht |
| Erscheint | bei Tag |
| Bleibt | verschwindet nie und wird mit der Welt gespeichert |
| Gruppengröße | bis 4 |
| Erscheinungsgewicht | 2 |
| Obergrenze | alle friedlichen Wesen zusammen: 6 pro Spieler |
| Vermehrt sich mit | [Karotte](food.md#item-qtcraft-carrot), [Kartoffel](food.md#item-qtcraft-potato) |
| Nachwuchs | wieder nach 5 min, Junge wachsen in 20 min heran |
| Beute | [Rohes Schweinefleisch](food.md#item-qtcraft-raw_porkchop) (2×) |

**Erscheint auf**

[Grasblock](blocks.md#block-qtcraft-grass_block)

**Erscheint in Biomen**

Alle Biome.

### <a id="mob-qtcraft-zombie"></a>Zombie

Kommt nachts heraus und lebt in Höhlen. Verfolgt Spieler, die er sieht, und schlägt hart zu.

| | |
|---|---|
| ID | `qtcraft:zombie` |
| Verhalten | feindlich: verfolgt und greift Spieler an |
| Gesundheit | 20 Lebenspunkte |
| Größe | 0,6 m breit, 1,95 m hoch |
| Geschwindigkeit | 1,2 m/s beim Gehen, 3,2 m/s beim Verfolgen |
| Angriff | 3 Schaden bis 1,6 m, alle 1 s |
| Sieht Spieler bis | 20 m |
| Erscheint | nachts |
| Höhlen | auch in Höhlen zu jeder Zeit |
| Licht beim Erscheinen | Blocklicht bis 7 |
| Gruppengröße | bis 3 |
| Erscheinungsgewicht | 3 |
| Obergrenze | alle feindlichen Wesen zusammen: 8 pro Spieler |
| Beute | [Verrottetes Fleisch](food.md#item-qtcraft-rotten_flesh), [Karotte](food.md#item-qtcraft-carrot), [Kartoffel](food.md#item-qtcraft-potato) |

**Erscheint auf**

[Erde](blocks.md#block-qtcraft-dirt), [Grasblock](blocks.md#block-qtcraft-grass_block), [Sand](blocks.md#block-qtcraft-sand), [Schneeblock](blocks.md#block-qtcraft-snow_block), [Stein](blocks.md#block-qtcraft-stone)

**Erscheint in Biomen**

Alle Biome.
