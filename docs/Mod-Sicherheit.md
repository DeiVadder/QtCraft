# Mod-Sicherheit – Stand und offene Fragen

Mod-Sicherheit (Sandbox, Rechte, Signaturen) ist laut Auftrag ein **eigenes späteres Projekt**. Dieses Dokument hält fest, was das heutige Design dafür bereits mitbringt und welche Fragen offen sind.

## Was das Design heute sicherstellt

- **Nur Daten, kein Code.** Mods bestehen aus JSON-Definitionen und PNG-Texturen. Es gibt keine Skript-Engine, keine Plugins und keine dynamischen Bibliotheken.
- **Eine einzige Lesestelle.** Nur `QcContentPackLoader` liest Dateien eines Pakets, und nur unterhalb des Paketverzeichnisses aus festen Unterordnern (`blocks/`, `items/`, …). Pfade aus Mod-Daten werden nie als Dateipfade benutzt; Texturnamen entstehen aus Dateinamen, nicht umgekehrt. Gelesen werden nur reguläre Dateien in echten Verzeichnissen: symbolische Links (Mod-Ordner, Unterordner, Dateien), Pipes und Gerätedateien werden übersprungen, gelesen wird höchstens 4 MiB pro Datei.
- **Schmale Schnittstelle.** Alles aus einem Mod landet über `QcContentRegistry` als geprüfte Werte-Strukturen im Spiel. Kein Mod-Inhalt erhält Zugriff auf Dateisystem, Netzwerk oder interne Objekte. Server und Client sehen nur die Registry.
- **Validierung an der Grenze.** Jede Datei ist auf 4 MiB begrenzt, es werden höchstens 256 Mod-Verzeichnisse betrachtet; ein Mod, der den Inhalt über die Grenzen der Clients hebt (Anzahl je Art, 32 MiB Gesamtgröße), wird samt abhängigen Mods übersprungen. Der Parser prüft Typ und Wertebereich jedes Felds und überspringt fehlerhafte Einträge. Abhängigkeiten werden aufgelöst, ohne einem Mod zu vertrauen; Zyklen und fehlende Abhängigkeiten führen zum Überspringen.
- **Clients lesen keine Mod-Dateien.** Der Server schickt die fertige Registry (`ContentSync`); der Client prüft sie beim Einlesen erneut (Größen, Zahlenbereiche, Rezeptformen). Ein späterer Mehrspieler-Client führt also nichts aus, was ein Server-Mod mitbringt.
- **Sandboxing ohne Umbau möglich.** Da das Laden in einer Klasse ohne Abhängigkeiten zu Server-Logik oder UI gekapselt ist, kann es später in einen eigenen Prozess oder eine eingeschränkte Umgebung verlagert werden. Übergeben wird dann nur die serialisierte Registry, deren Format (`QcContentRegistry::writeTo`/`readFrom`) es schon gibt.

## Bekannte Restrisiken

- **Ressourcenverbrauch.** Die Dateien eines Mods werden vor dem Lesen gemessen (auch Hardlinks auf eine große Datei zählen mehrfach); alle Mods zusammen dürfen höchstens 64 MiB lesen lassen, gezählt wird jeder Leseversuch (auch abgelehnte und erneut geladene Mods). Jeder Mod wird in eine Kopie der Registry geladen (Qt-Container teilen ihre Daten, die Kopie ist billig) und sofort so geprüft, wie sein Inhalt beim Client ankäme (Anzahlen, die der Client annimmt, und codierte Größe samt der Items für Blöcke); höchstens 64 Mods werden pro Start geladen und geprüft (auch abgelehnte zählen). Damit sind Zeit und Speicher eines Starts begrenzt (64 Prüfungen von höchstens 32 MiB). Prozedurale Texturen behalten nur ihre bekannten Felder (kurze Texte, Zahlen), beliebiges weiteres JSON wird nicht übernommen. Ein abschließendes Zurücklesen wie beim Client ist das Sicherheitsnetz; schlägt es je fehl, startet das Spiel ohne Mods. Die Weltgenerierung ist über Obergrenzen für Erze (Anzahl, Erzblöcke je Chunk), Biome, Features und Feature-Platzierungen begrenzt; PNG-Texturen über 128 × 128 Pixel werden vor dem Dekodieren abgelehnt. Speicher, den der Allokator nach dem Parsen großer JSON-Dateien behält, bleibt ein Restrisiko. Ein Mod kann über `ores`/`features` die Weltgenerierung verlangsamen.
- **Bildformate.** PNG-Dateien werden mit `QImageReader` dekodiert (Bildgröße wird vorher geprüft). Fehler in Bild-Decodern sind eine klassische Angriffsfläche; eine Isolierung gibt es nicht.
- **Spielregeln.** Ein Mod darf Basisinhalte ersetzen und so z. B. unzerstörbare Blöcke, extrem starke Waffen oder Rezepte aus dem Nichts definieren. Das ist gewollt (Mods ändern Inhalte), muss im Mehrspieler aber vom Server-Betreiber kontrolliert werden.
- **Windows-Junctions** gelten für Qt nicht als symbolische Links; ein Mod-Ordner als Junction wird deshalb gelesen. Für Windows ist eine zusätzliche Prüfung nötig.
- **Wechselnde Dateien.** Zwischen Prüfung (`QFileInfo`) und Öffnen könnte eine Datei durch einen Link ersetzt werden (TOCTOU); wer das kann, hat aber bereits Schreibrechte im Mod-Ordner.
- **Herkunft.** Es gibt keine Signaturen und keine Prüfung, woher ein Mod stammt oder ob er seit der Installation verändert wurde.
- **Übersetzungstexte** landen in QML-Labels. Sie werden als einfacher Text angezeigt; sollte je Rich Text verwendet werden, wäre das eine neue Angriffsfläche.

## Offene Fragen für das Sicherheitsprojekt

1. Sollen Mods signiert werden, und wer stellt Schlüssel aus (Spieler, Server-Betreiber, zentrale Stelle)?
2. Braucht es Gesamtgrenzen (Texturen, Definitionen, Weltgenerierungs-Aufwand) und eine Messung der Ladezeit pro Mod?
3. Sollen Bilder in einem eigenen Prozess oder mit begrenzter Größe vor dem Dekodieren geprüft werden?
4. Wie werden Mods im Mehrspieler abgeglichen: Muss der Client dieselben Mods haben (nein, heute nicht nötig) oder nur akzeptieren, was der Server schickt? Darf ein Spieler sehen, welche Mods ein Server nutzt, bevor er beitritt?
5. Falls später Skript-Hooks (`QJSEngine`) kommen: welches Rechte-Modell (Ereignisse lesen, Blöcke setzen, Chat), welche Zeit- und Speichergrenzen, und wie verhindert man Zugriff auf `QObject`s des Spiels?
6. Wie werden Spieler gewarnt, wenn ein Mod Basisinhalte ersetzt (Liste der ersetzten Namen im Mods-Bildschirm)?
7. Sollen Mods für einzelne Welten statt global aktiviert werden?
