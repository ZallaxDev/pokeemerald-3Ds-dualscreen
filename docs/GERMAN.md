# Pokémon Smaragd-Edition auf Deutsch

Pokémon Emerald 3Ds Dual Screen arbeitet mit der sauberen deutschen ROM von
**Pokémon Smaragd-Edition (Deutschland, BPED)**, mit ihren Texten und ihren Grafiken. Die
Übersetzung wird nicht mitgeliefert: sie kommt aus deiner ROM, auf deinem Rechner oder im
Browser.

Der Datenbestand ist gegen BPED selbst geprüft: das Manifest ist zusammengestellt (19 310
Textstellen, 104 Grafik-Ressourcen, die 275 Credit-Einträge, die vier Tische des
Trainerturms), und `tools/localize_german.py` läuft auf einem Arbeitsbaum durch. Die
Variante wurde aber **noch nicht kompiliert**, und das Spiel wurde weder auf einer Konsole
noch im Emulator bis zum Ende gespielt.

## Installieren

Wie die englische Fassung: der Web-Builder und der Windows-Builder erkennen alle
unterstützten ROMs und wählen die Sprache selbst. Jede Release enthält die Varianten, jede
mit ihrem Rezept. Für die schnelle Aktualisierung (nur das 3DSX) prüft die Webseite dein
`emerald3ds.pak` und bietet dir `Emerald3DS-de.3dsx` an, das du als `Emerald3DS.3dsx` auf
die SD-Karte legst.

| Sprache | Code | SHA-1 der sauberen ROM |
|---|---|---|
| Deutsch | BPED | `61c2eb2b380b1a75f0c94b767a2d4c26cd7ce4e3` |
| Englisch | BPEE | `f3ae088181bf583e55daf962a92bb46f4f1d07b7` |
| Französisch | BPEF | `ca666651374d89ca439007bed54d839eb7bd14d0` |
| Italienisch | BPEI | `1692db322400c3141c5de2db38469913ceb1f4d4` |
| Spanisch | BPES | `fe1558a3dcb0360ab558969e09b690888b846dd9` |

## Die deutsche Fassung aus dem Quelltext bauen

Mit den Voraussetzungen aus der [Entwicklungsanleitung](DEVELOPMENT.md):

```sh
python tools/bootstrap.py --make --german-rom "/Pfad/Pokemon - Smaragd-Edition (Germany).gba" -j8
```

Der Bootstrap wendet die Patches an, baut die Werkzeuge und die generierten Includes, führt
`tools/localize_german.py` mit deiner ROM aus und baut das 3DSX nach
`build/upstream/3ds_port/emerald3ds.3dsx`. Ein Sprachwechsel (Neustart ohne
`--german-rom`) setzt den Baum zurück und löscht die Übersetzungsobjekte und
-ressourcen, damit keine Daten einer anderen Sprache wiederverwendet werden.

Die Engine prüft, für welche Sprache sie gebaut wurde: eine deutsche Ausführungsdatei
akzeptiert nur einen Datenbestand aus BPED, und umgekehrt.

## Wie die deutsche Fassung entsteht

`tools/localize_german.py` prüft den SHA-1 der ROM und die SHA-256-Abdrücke der
Quelldateien, bevor etwas in den generierten Baum geschrieben wird. Die Manifeste in
`tools/locales/` enthalten Positionen im Quelltext, Verschiebungen und Größen in der ROM,
Symbolnamen und Adressen. Sie enthalten keine Texte, Grafiken, Klänge oder
ROM-Abschnitte.

Der Ablauf ersetzt Texte und Codefelder durch die deiner ROM, holt 104 Grafiken heraus und
baut die 275 Credit-Einträge sowie die vier Trainerturm-Etagen neu auf. Dadurch laufen
Namen, Dialoge, Menüs, das Wörterbuch, die Bardenlieder, die Trainer-Sprüche, die Karten,
die Fragen und die Brailleschrift der Ruinen auf Deutsch.

Deutsches:

- die fünf Schriften (und die übrigen 99 Grafiken) kommen aus der deutschen ROM;
- der Bildschirm « ENDE » benutzt die vorgerenderte Tilemap der deutschen ROM
  (`graphics/credits/german_the_end.bin.lz`), über
  `patches/pokeemerald/0051-german-ending-tilemap.patch`.

Noch nicht fertig, und deshalb offen ausgewiesen:

- die **Feinheiten des Layouts** (die Entsprechung zu `0049-italian-layout.patch`:
  Nachrichtenbreiten, Dezimalkomma und Ausrichtung im Pokédex, Länge der
  Item-Beschriftungen) werden erst beim Bauen der Variante entschieden;
- das Verzeichnis der Symbole (`tools/locales/german-symbols.s.gz`) entsteht erst aus einem
  vollständigen Build.

Die Menüs des Ports (EINSTELLUNGEN, VERBESSERUNGEN, CHEATS und ihre Werte) sind über
`CTR_TEXT` in `3ds_port/include/3ds_locale.h` an fünf Sprachen gebunden, auf Deutsch
eingetragen; die Umlaute und das scharfe ß wandelt `3ds_locale_text.h` in die Zeichencodes
der internationalen Tabelle um (`Ä`=F1, `Ö`=F2, `Ü`=F3, `ä`=F4, `ö`=F5, `ü`=F6, `ß`=15).

Wenn eine Änderung des Ports eine Quelldatei des Manifests verändert, hält die
Lokalisierung an (« Source differs from the pinned patched tree »), bis ihre Positionen und
Abdrücke aktualisiert sind.

## Fotos für die Durchsicht

Die Variante muss noch auf einer Konsole ausprobiert werden. Den unteren Bildschirm
vollständig fotografieren, ohne Ränder oder Tabs zu beschneiden:

- EINSTELLUNGEN, Karteikarte der Optionen: die zwölf Zellen, die Werte und die Tabs.
- VERBESSERUNGEN: die Optionen, einschließlich der vom Port ergänzten.
- CHEATS: die beiden Seiten, mit Seitenzahl und sichtbaren Werten.
- Aus POKéMON die Zusammenfassung eines Pokémon: Name, Typen, Werte, Wesen, Fähigkeit und
  Attacken mit AP; am besten lange Namen.
- Im Kampf die Attackenwahl: die vier Felder mit Namen, Typen und AP.
- Der Pokédex: Größe und Gewicht eines Pokémon, um Meter, Kilogramm und das Dezimalkomma zu
  prüfen.
- Die Credits und der Schlussbildschirm « ENDE ».

Den gebauten Commit und das Konsolenmodell zu den Fotos notieren. Breitenprüfungen am
Rechner ersetzen diese Fotos und die Versuche auf der Konsole nicht.

Der Port benutzt feste SD-Pfade (`/3ds/emerald3ds/`) für die Daten, die Einstellungen und
den Spielstand, auch wenn das 3DSX aus einem anderen Ordner gestartet wird. Für einen
Testlauf ohne eine bestehende Installation anzufassen, eine eigene Test-SD-Karte benutzen.

## Tests ohne ROM

```sh
python -m unittest discover -s builder/tests -v
```

## Autor

Deutsche Fassung und Lokalisierungswerkzeuge von andyst-dev.
