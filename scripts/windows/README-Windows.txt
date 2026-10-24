QtCraft @VERSION@ für Windows (x64), Stand @COMMIT@
====================================================

Starten
-------
1. Vor dem Entpacken: Rechtsklick auf die heruntergeladene ZIP-Datei → „Eigenschaften“ → unten
   bei „Sicherheit“ das Häkchen „Zulassen“ setzen → „OK“. Dann gelten die entpackten Dateien
   nicht als „aus dem Internet“, und Windows fragt seltener nach. (Steht dort kein „Zulassen“,
   ist nichts zu tun.)
2. Die ZIP-Datei ganz entpacken (Rechtsklick → „Alle extrahieren …“), z. B. nach
   Dokumente\QtCraft. Nicht direkt aus der ZIP-Datei heraus starten.
3. Im entpackten Ordner qtcraft.exe doppelklicken. Eine Installation oder Administratorrechte
   braucht es nicht; die nötigen Laufzeit-Bibliotheken liegen im Ordner.
4. Windows zeigt beim ersten Start „Der Computer wurde durch Windows geschützt“ (SmartScreen),
   weil das Programm nicht signiert ist: auf „Weitere Informationen“ klicken, dann auf
   „Trotzdem ausführen“.

Hinweis: Ist unter Windows 11 die „Intelligente App-Steuerung“ (Smart App Control) eingeschaltet,
blockiert sie unsignierte Programme ohne „Trotzdem ausführen“. Dann bitte der Spielleitung
Bescheid geben (Windows-Sicherheit → App- und Browsersteuerung → Intelligente App-Steuerung zeigt
den Zustand; einmal ausgeschaltet, lässt sie sich nur mit einer Neuinstallation wieder
einschalten).

Voraussetzung: Windows 10 oder 11, 64 Bit (x64), eine Grafikkarte mit Direct3D 11 oder 12.

Einzelspieler
-------------
Im Hauptmenü eine neue Welt erstellen und spielen. Der Server läuft dabei im selben Programm,
ohne Netz; die Windows-Firewall fragt nicht.

Mehrspieler im LAN (Server auf dem Mac)
---------------------------------------
Hauptmenü → „Mehrspieler“ → Serveradresse eintragen:

    @SERVER@

Spielername wählen, ein Passwort nur eintragen, wenn die Spielleitung eines nennt, dann
„Beitreten“. Spiel und Server müssen aus demselben Stand gebaut sein (Version und Commit oben),
sonst lehnt der Server die Anmeldung ab.

Die Windows-Firewall fragt nur, wenn ihr selbst eine Welt mit „Im LAN öffnen“ (Pausenmenü) für
andere öffnet; dann den Zugriff im privaten Netzwerk erlauben.

Protokolle und Absturzberichte
------------------------------
Das Spiel schreibt bei jedem Start ein Protokoll (die letzten 10 Sitzungen bleiben). Alles liegt im
Datenordner des Spiels:

    %APPDATA%\QtCraft\QtCraft
    (z. B. C:\Users\<Name>\AppData\Roaming\QtCraft\QtCraft)

    logs\              Protokolle, Absturzberichte (*.crash.txt) und Speicherabbilder (*.dmp)
    diagnostics\       gesammelte Diagnosepakete

Am einfachsten: Einstellungen → Debug → „Protokollordner öffnen“.

Nach einem Absturz zeigt das Hauptmenü beim nächsten Start einen Hinweis mit dem Pfad des
Berichts.

Etwas ist schiefgegangen? Einstellungen → Debug → „Diagnose sammeln“. Das Spiel legt einen Ordner
diagnostics\qtcraft-<zeit> an und öffnet ihn: Rechtsklick auf den Ordner → „In ZIP-Datei
komprimieren“ und die ZIP-Datei nur an die Spielleitung schicken (ein Speicherabbild kann ein in
dieser Sitzung eingetipptes Server-Passwort enthalten). Bitte dazu notieren, wann (Uhrzeit) es
passierte und was ihr gerade getan habt.

Weiteres
--------
- Anzeige: Einstellungen → Anzeige (Fenster, randloses Vollbild, Vollbild, VSync,
  Bildraten-Limit); F11 schaltet Vollbild um. Lüfter laut: Limit 60 setzen.
- qtcraft-headless.exe im selben Ordner ist der eigenständige Server (für den Playtest nicht
  nötig; „qtcraft-headless.exe --help“ in einer Eingabeaufforderung zeigt die Optionen).
- Der Ordner mods enthält das Beispiel-Mod „rubies“; es wird beim ersten Start ausgeschaltet in
  den Datenordner übernommen.
- Lizenzen: LICENSE (GPL 3, das Spiel), THIRD_PARTY.md, CREDITS.md und der Ordner licenses (Qt unter
  der LGPL 3 mit Qt-NOTICE.txt, SDL3, HIDAPI, SDL-libm, OpenSSL, Schrift Pixelify Sans).
- libssl-3-x64.dll und libcrypto-3-x64.dll neben qtcraft.exe sind OpenSSL (Apache License 2.0):
  damit verschlüsselt das Spiel die Verbindungen im Mehrspieler (TLS). Nicht löschen.
