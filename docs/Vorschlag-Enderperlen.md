# Vorschlag: Mit Enderperlen durch Blöcke (Grundgestein-Decke)

Stand: 2026-10-14, vom Menschen gewünscht. Noch nicht beauftragt; der Mensch legt Etappe und Zeitpunkt fest.

## Entscheidung vorab

Grundgestein bleibt unzerstörbar und unbeweglich. Es gibt keinen Weg über Kolben oder Explosionen. Der Stand dazu wurde am 2026-10-14 auf `feature/31-2-entities` geprüft: Härte −1 sperrt Abbauen, Kolben, Platzieren und alle anderen Schreibwege. Das bleibt so.

Den Reiz der Vanilla-Tricks (Bauten, die sonst im Überlebensmodus unmöglich wären) bietet stattdessen ein anderer Weg: **Wer mit dem Kopf in einem Block steckt, kann sich mit Enderperlen Lage für Lage hindurchwerfen**, auch durch eine Decke aus Grundgestein.

## Voraussetzungen (beides gibt es noch nicht)

- **Schienen und Loren:** geplant als Etappe 32; dort entsteht der Weg mit dem Kopf in die Decke (siehe unten).
- **Enderperlen:** werfbares Item, Flugbahn wie andere Würfe, beim Aufprall wird der Werfer an die Aufprallstelle versetzt (Schaden, Abklingzeit und Herkunft der Perlen legt die Etappe fest).
- **Eine Grundgestein-Decke:** eine Dimension oder Weltart mit Dach aus Grundgestein, über dem Platz ist, der sich zum Bauen lohnt. Die heutige Welt hat nur einen Boden bei y = 0.

## Regel (Entwurf, vom Menschen 2026-10-14)

> Trifft eine Enderperle die **Innenseite** eines Blocks (sie startet in einem festen Block, weil der Kopf des Werfers darin steckt, und verlässt ihn durch eine Fläche), landet der Werfer auf der **Außenseite** dieser Fläche, also in der Nachbarzelle dahinter.

So wird das Durchqueren einer Decke zum Vanilla-artigen Trick:

1. Den Kopf in die Decke bekommen (siehe „Voraussetzung im Spiel“).
2. Eine Perle nach oben werfen: Sie trifft die Innenseite der oberen Fläche, und der Spieler steckt danach eine Lage höher.
3. Ist auch diese Zelle fest (mehrlagige Decke), steckt der Spieler wieder im Block, und das Ganze beginnt von vorn: Perlen werfen, bis man oben im Freien ist.

- **Kosten:** Jede Lage kostet eine Perle (Schaden und Abklingzeit der Perle wie üblich), und solange die Augen in einem festen Block stecken, nimmt der Spieler Erstickungsschaden (`QcServerCombat::applySuffocation`, gibt es schon). Eine dicke Decke ist damit ein echter Wettlauf.
- **Allgemeine Regel, nicht nur Grundgestein:** Sie gilt für jeden festen Block, auch für Wände. Das ist ehrlicher und leichter zu verstehen als eine Sonderregel für die Decke. Folge: Wer den Kopf in eine Wand bekommt, kann sich durchperlen; Server, die das nicht wollen, schalten die Spielregel ab (siehe unten).
- **Deterministisch:** Ob der Wurf in einem Block startet und durch welche Fläche er ihn verlässt, folgt aus der Strahlverfolgung der Perle; das Ziel ist die Nachbarzelle hinter dieser Fläche. Die Regel steht ausdrücklich in der Teleport-Logik der Perle, als feste Regel und nicht als Lücke in der Kollision. Sie gilt gleich an jeder Stelle und in jeder Richtung.
- **Serverseitig:** Ob der Kopf im Block steckt, entscheidet der Server aus seiner Position des Spielers, nicht der Client.

## Voraussetzung im Spiel: den Kopf in die Decke bekommen (Lore und Treppe)

Heute verhindert die Physik, dass ein Spieler in Blöcke gerät. Der Weg hinein soll wie in Vanilla über **Lore und Treppe** gehen (Mensch, 2026-10-14). Beides sind gewöhnliche Spielmittel, nichts eigens für den Trick Gebautes:

- Eine Lore steht auf einer Treppe (oder auf einer Schiene, die über eine Treppe läuft) direkt unter der Decke. Die erhöhte Lage auf der Treppe plus die Sitzhöhe in der Lore heben die Augen des Spielers in die unterste Lage der Decke.
- Beim Aussteigen (oder schon beim Sitzen, je nach Ausgestaltung) bleibt der Spieler an dieser Position: Aussteigen setzt ihn auf die Sitzposition, ohne ihn aus dem Block zu schieben. Von dort startet die erste Perle in der Decke.
- Die genaue Mechanik (Sitzhöhe, wo Aussteigen absetzt, ob eine Lore auf einer Treppe stehen darf, ob man aus der fahrenden Lore werfen kann) legt **Etappe 32 (Schienen und Loren)** fest. Dort muss sie so gebaut werden, dass dieser Weg möglich bleibt; der Test dazu gehört in Etappe 32, die Perlen-Regel in die Etappe mit den Enderperlen.

Wichtig dabei: Spieler, die in einem Block stecken, dürfen von der Physik nicht nach unten herausgeschoben werden (Kreaturen schiebt `QcMobUpkeep` heute an den nächsten freien Platz, E-352). Sonst endet der Trick, bevor die erste Perle fliegt. Ersticken bleibt der Preis.

## Tests und Schutz

- Szenario-Test (Etappe 32): Lore auf einer Treppe unter einer Decke, Spieler steigt ein und aus und steckt danach mit den Augen in der untersten Lage; Gegenprobe ohne Treppe oder mit höherer Decke: kein Steckenbleiben.
- Szenario-Test (Etappe mit den Perlen): Spieler mit dem Kopf in einer dreilagigen Decke aus Grundgestein, drei Perlen nach oben, danach steht er oben im Freien und hat Erstickungsschaden genommen; dasselbe seitlich durch eine Wand und in allen Richtungen. Gegenproben: Perle von außen gegen die Decke (kein Durchkommen), Kopf nicht im Block, abgeschaltete Spielregel, Client behauptet eine Position im Block, die der Server nicht hat.
- Eine Entscheidung (E-Nummer) hält fest, dass das gewollt ist. Ein Kommentar im Test kennzeichnet es, damit ein späterer Umbau der Perlen-Kollision es nicht entfernt.
- **Keine Showcase-Station:** Die Showcase-Welt ist im Hauptmenü wählbar und würde den Trick verraten. Das ist eine begründete Ausnahme von der Definition of Done ab Etappe 21; der Szenario-Test deckt die Regel ab.
- Ein Test prüft, dass kein Handbuchtext (beide Sprachen, auch der Export in `docs/handbook` und `web/handbook/data`) den Trick beschreibt, etwa über eine Liste verbotener Begriffe in den Seiten zu Perle, Decke und Spielregel.

## Offene Fragen an den Menschen

- Welche Etappe bringt Enderperlen, und welche Dimension oder Weltart bringt die Decke?
- Bleibt der Spieler schon beim Sitzen in der Lore im Block stecken, oder erst beim Aussteigen? Darf man aus der Lore heraus Perlen werfen?
- Gilt die Regel für jeden festen Block oder nur für Grundgestein? Der Entwurf sagt „jeder Block“.
