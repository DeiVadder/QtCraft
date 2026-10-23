# Playtest – Anleitung

Kurzanleitung für den Playtest (Etappe 74): ein dedizierter Server auf dem MacBook, die Spieler an
Windows-PCs. Ziel: Was beim Spielen schiefgeht, lässt sich hinterher aus Protokollen,
Absturzberichten und Speicherabbildern klären. Hintergrund und Einzelheiten: Entscheidung E-834,
Optionen in `docs/Architektur.md` („Build options and test hooks“), Server allgemein in
`docs/Multiplayer.md`. Den Windows-Build (Qt-Kits, Module, Werkzeuge) beschreibt die README im
Abschnitt [„Windows (port in progress)“](../README.md#windows-port-in-progress).

## 1. Server auf dem Mac starten

```sh
# Einmal: Einstellungen anlegen lassen (server.json im Datenordner), dann anpassen
qtcraft-headless --server --console
# Für den Playtest: Protokoll ist bei --server immer an; Gesundheitszeile jede Minute (Standard)
QTCRAFT_SERVER_PASSWORD=geheim qtcraft-headless --server --console --world Playtest --max-players 4
# Mehr Einzelheiten zu Netz und Anmeldung im Protokoll
qtcraft-headless --server --console --log-rules "qtcraft.network.debug=true;qtcraft.server.debug=true"
```

- **Datenordner des Servers:** `~/Library/Application Support/QtCraft/qtcraft-headless` (anders als
  der des Spiels; mit `--data-dir <ordner>` frei wählbar). Darin `server.json`, `worlds/`, `logs/`.
- **Protokoll:** `logs/qtcraft-headless-<datum-uhrzeit>.log`. Kopf mit Version, Git-Commit, System,
  Welt, Seed und Paketen; danach jede Verbindung mit Programm, Version und System des Clients
  (`Hello from …`), Anmeldung, Ablehnung (z. B. Protokollversion), Verlassen mit Grund und Dauer,
  Kicks, Tick-Spitzen über 50 ms mit den längsten Phasen (`qtcraft.lag`) und jede Minute eine Zeile
  `HEALTH` (Speicher, Tickzeiten, Chunks, Entities, Spieler, Netzraten). `--health-log <s>` ändert
  den Abstand. Das Passwort steht nie im Protokoll.
- **Beenden:** `stop` in der Konsole oder Strg+C; beides speichert die Welt.
- **Diagnose sammeln:** in der Konsole `diagnostics` eintippen, oder nach dem Beenden (auch nach
  einem Absturz) `qtcraft-headless --write-diagnostics` (mit demselben `--data-dir`). Ergebnis:
  ein Ordner `diagnostics/qtcraft-headless-<zeit>/` mit Protokollen, Absturzberichten,
  `server.json` ohne Passwort und `info.txt`.
- **Wenn der Server selbst abstürzt:** neben dem eigenen Bericht in `logs/` legt macOS einen
  Absturzbericht unter `~/Library/Logs/DiagnosticReports/qtcraft-headless-*.ips` an (mit allen
  Threads); ihn mitschicken.

## 2. Ports und Firewall

- Der Server lauscht auf TCP-Port **25600** (`port` in `server.json` oder `--port`).
- macOS fragt nur, wenn die Firewall eingeschaltet ist (Systemeinstellungen → Netzwerk → Firewall),
  beim ersten Lauschen, ob eingehende Verbindungen erlaubt sind: **Erlauben**. Sonst dort unter
  „Optionen“ `qtcraft-headless` freigeben. Ein selbst gebautes, nur ad hoc signiertes Programm gilt
  nach jedem Neubau als neues Programm: die Frage kann dann wiederkommen.
- Im selben Netz verbinden die Spieler mit der IP-Adresse des Macs (Systemeinstellungen → WLAN →
  Details), z. B. `192.168.1.20:25600`. Über das Internet: Port 25600 im Router auf den Mac
  weiterleiten (keine NAT-Durchdringung, siehe `docs/Multiplayer.md`).
- Unter Windows fragt die Windows-Firewall nur, wenn ein Spieler seine Welt im Spiel **für das LAN
  öffnet** (das Spiel lauscht dann selbst); weder der Einzelspieler (der Server läuft im selben
  Prozess ohne Netz) noch das Beitreten zu einem Server brauchen eine Freigabe.

### Über das Internet ohne Router-Einstellung: Tailscale (Playtest)

QtCraft kann (noch) keine Verbindung durch NAT herstellen; ein eingebauter Weg ist offen
(`docs/FORTSCHRITT.md`, Paket 74.6). Für den Playtest verbindet Tailscale die Rechner in einem
privaten, mit WireGuard verschlüsselten Netz, ohne Portweiterleitung und auch hinter CGNAT/DS-Lite.
Das deckt bis zur TLS-Verschlüsselung (Etappe 48) auch ab, dass Name und Passwort sonst im Klartext
übers Netz gingen.

1. **Mac (Spielleitung):** Tailscale installieren und mit einem Konto (Google, Microsoft, GitHub …)
   anmelden. In der Admin-Konsole beim Mac **„Share…“** wählen und je Spieler einen Einladungslink
   erzeugen. Geteilt wird nur dieser eine Rechner; die Spieler sehen nichts sonst aus dem eigenen
   Netz. Der kostenlose Tarif reicht.
2. **Spieler (Windows):** Tailscale installieren, mit einem eigenen Konto anmelden, den
   Einladungslink öffnen und annehmen. Der Mac erscheint dann in ihrer Tailscale-Liste.
3. **Server** wie in Abschnitt 1 starten (Port 25600); die macOS-Firewall-Frage wie oben mit
   **Erlauben** beantworten.
4. **Verbinden:** im Spiel Mehrspieler → Adresse `<Name des Macs>:25600` (MagicDNS-Name aus der
   Tailscale-Liste) oder die Tailscale-Adresse `100.x.y.z:25600`.
5. **Vorher selbst prüfen**, z. B. von einem zweiten Rechner oder aus der Windows-VM mit eigenem
   Tailscale-Konto. In der Tailscale-Liste zeigt `tailscale status` (bzw. die App), ob die Verbindung
   direkt läuft oder über ein Relay (dann etwas mehr Latenz, funktioniert aber).

QtCraft hat keine automatische Server-Suche im LAN, die Adresse wird immer eingetippt; ein
Ebene-2-Netz (ZeroTier, OpenVPN mit TAP) bringt daher keinen Vorteil.

## 3. Für die Spieler (Windows)

- **Anzeige** (E-835): Einstellungen → Anzeige wählt Fenster, randloses Vollbild oder Vollbild samt
  Monitor, VSync (gilt nach einem Neustart) und ein Bildraten-Limit (30–144 oder unbegrenzt) mit
  eigenem Limit für Menüs, Pause und Hintergrund (Standard 30); **F11** schaltet Vollbild ein und
  zurück. Lüfter laut oder Grafikkarte heiß: ein Limit von 60 setzen. Am ruhigsten läuft das Bild
  mit VSync ohne Limit.
- **Prüfpunkt Bildraten-Limit (Spielleitung):** Limit 60 an einem 60-Hz-Monitor und an einem
  120- oder 144-Hz-Monitor einstellen, in der Welt gehen und sich umsehen: ruckelt das Bild
  gegenüber „Unbegrenzt“ sichtbar (Timer-Takt gegen VSync, E-835), bitte mit Monitor und Hz notieren.
- **Protokoll ist standardmäßig an.** Jeder Start schreibt ein Sitzungsprotokoll; die letzten 10
  Sitzungen bleiben. Abschalten (nicht empfohlen): Einstellungen → Debug → „Protokolldatei
  schreiben“ (gilt ab dem nächsten Start).
- **Wo die Dateien liegen** (Ordner `logs` im Datenordner des Spiels):

  | System | Datenordner des Spiels |
  |---|---|
  | Windows | `%APPDATA%\QtCraft\QtCraft` (z. B. `C:\Users\<Name>\AppData\Roaming\QtCraft\QtCraft`) |
  | macOS | `~/Library/Application Support/QtCraft/QtCraft` |
  | Linux | `~/.local/share/QtCraft/QtCraft` |

  Am einfachsten: Einstellungen → Debug → **„Protokollordner öffnen“**.
- **Nach einem Absturz** zeigt das Hauptmenü beim nächsten Start einen Hinweis mit dem Pfad des
  Berichts. Unter Windows liegt daneben ein Speicherabbild `qtcraft-<zeit>.dmp` (Stacks und
  referenzierter Speicher; mit `--full-dump` auf der Kommandozeile der ganze Speicher, groß).
- **Was zurückschicken:** Einstellungen → Debug → **„Diagnose sammeln“**. Das Spiel legt den Ordner
  `diagnostics\qtcraft-<zeit>` im Datenordner an und öffnet ihn: Rechtsklick auf den Ordner →
  „In ZIP-Datei komprimieren“ (Windows 11; älter: „Senden an → ZIP-komprimierter Ordner“), die
  ZIP-Datei an die Spielleitung schicken. Enthalten sind die Protokolle der letzten drei Sitzungen,
  alle Absturzberichte und Speicherabbilder, `settings.ini` (ohne Passwörter) und `info.txt` (Version,
  System, Mods, Weltnamen); nie der Spielerschlüssel (`player.key`) oder Welten. Ein
  Speicherabbild (`.dmp`) enthält Speicher des Spiels und damit womöglich ein in dieser Sitzung
  eingetipptes Server-Passwort: das Paket nur an die Spielleitung schicken.
- Bitte dazu kurz notieren, **wann** (Uhrzeit) etwas passierte und was ihr gerade getan habt: die
  Protokollzeilen tragen absolute Zeiten.

## 4. Optionen für Fortgeschrittene (Spiel und Server)

| Option | Wirkung |
|---|---|
| `--log-file <datei>` | Protokoll in diese Datei statt in `logs/` |
| `--no-log-file` | kein Protokoll (Absturzberichte gibt es trotzdem) |
| `--log-rules "<regeln>"` | Logging-Regeln wie `QT_LOGGING_RULES`, z. B. `qtcraft.network.debug=true` |
| `--health-log <s>` | Abstand der `HEALTH`-Zeilen in Sekunden, 0 = aus (Standard 60) |
| `--full-dump` | Windows: Speicherabbild mit dem ganzen Speicher |
| `--stats <ms>` | feine Messwerte als JSON-Zeilen `STATS` (für Auswertungen, nicht für den Playtest nötig) |

Unter Windows startet man das Spiel mit Optionen über eine Verknüpfung (Eigenschaften → Ziel:
`…\qtcraft.exe --health-log 30`).

## 5. Auswerten (Spielleitung)

- Absturzbericht (`*.crash.txt`, nach dem Hinweis `*.crash-seen.txt`): Ursache, Adresse, Thread,
  Stack. macOS/Linux mit Funktionsnamen; Windows mit `Modul+Offset`.
- Windows-Speicherabbild (`*.dmp`): in Visual Studio oder WinDbg öffnen, mit den PDB-Dateien des
  ausgelieferten Builds (Paket 74.2 legt sie zum Build) und den Symbolen von Qt: im Qt-Installer
  die Komponente **„Qt Debug Information Files“** installieren, dann zeigt der Stack auch die Qt-
  Funktionen. Der Build muss zum Abbild passen: Version und Git-Commit stehen im Kopf des
  Protokolls und des Berichts.
- `HEALTH`-Zeilen zeigen, ob Speicher wuchs, Bildrate oder Tickzeit einbrachen; `Lag spike`-Zeilen
  des Servers nennen die teuersten Phasen; `Hello from`/`left after` zeigen Verbindungsabbrüche.
  Hängt ein Tick länger als 10 s, schreibt der Server statt der `HEALTH`-Zahlen des Servers eine
  Warnung `Server tick running for … s, the last phase that finished: …`.
