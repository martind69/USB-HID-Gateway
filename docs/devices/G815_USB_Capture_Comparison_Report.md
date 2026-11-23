# Logitech G815 – Vergleich Original-Capture vs. Passthrough-Capture

**Dokumenttyp:** Technischer Vergleich / Übergabe zur Weiterbearbeitung  
**Projekt:** USB-Gateway / transparenter HID-Passthrough  
**Gerät:** Logitech G815 RGB Mechanical Keyboard  
**Original-Capture:** `usb.pcap`  
**Passthrough-Capture:** `g815_test_passthrough.pcapng`  
**Capture-Werkzeuge laut Testbeschreibung:** USBPcap und Wireshark  
**Stand:** 22.11.2026  

---


## 1. Ziel

Dieser Report vergleicht den umfangreichen Originalmitschnitt mit einem kleineren Mitschnitt, der durch die USB-Passthrough-Strecke aufgenommen wurde. Ziel ist, festzustellen, welche Kommunikation im Passthrough-Capture sichtbar ist, welche Unterschiede auffallen und welche Aussagen derzeit belastbar bzw. noch vorläufig sind.

Der Vergleich ist insbesondere für die Entwicklung eines transparenten HID-Proxys im USB-Gateway relevant. Das Gateway soll nicht nur Standard-Keyboard-Reports transportieren, sondern auch Logitech-spezifische Kommunikation unverändert weiterreichen können.

**Wichtige Einschränkung:** Die vorliegenden Capture-Statistiken und die früheren Einzelinterpretationen haben unterschiedliche Verifikationsgrade. In diesem Dokument werden bestätigte Dateifakten, frühere Auswertungsergebnisse und noch zu prüfende Interpretationen getrennt gekennzeichnet. Für einen endgültigen Paket-für-Paket-Protokollbericht ist eine erneute vollständige Dissektion mit Wireshark/USBPcap oder einem verifizierten Parser erforderlich.

## 2. Capture-Testablauf

Die tatsächliche Testsequenz des ursprünglichen Mitschnitts wurde vom Anwender wie folgt bestätigt. Die Schritte 7–9 sind in dieser Reihenfolge **Helligkeit → statische Farbe → Effekt**.

| Nr. | Aktion | Untersuchungsziel |
|---:|---|---|
| 1 | G HUB vollständig schließen | Ausgangszustand ohne G HUB-Steuerung |
| 2 | Capture starten | Aufzeichnung vor Geräteanschluss beginnen |
| 3 | G815 einstecken | USB-Enumeration, Deskriptoren, Initialisierung |
| 4 | 10 s warten | Kommunikation bei geschlossenem G HUB beobachten |
| 5 | G HUB starten | Softwarestart und Gerätesteuerung |
| 6 | Geräteerkennung abwarten | Initialisierung und Konfiguration |
| 7 | Beleuchtungshelligkeit ändern | Helligkeitsparameter untersuchen |
| 8 | Statische Farbe ändern | Farb-/RGB-Parameter untersuchen |
| 9 | Effekt ändern | Effekt-/Modusparameter untersuchen |
| 10 | M1/M2/M3, Game Mode und G-Tasten betätigen | Profil-, Modus- und Sondertasten-Reports untersuchen |
| 11 | G HUB schließen | Kommunikation beim Beenden der Software beobachten |
| 12 | Capture stoppen | Aufzeichnung abschließen |

Die einzelnen Aktionen wurden nicht mit präzisen Zeitmarken oder Capture-Kommentaren versehen. Daher sind Aktion 7, 8, 9 und insbesondere die mehreren Betätigungen in Schritt 10 nicht bytegenau voneinander isoliert. Das ursprüngliche Capture ist ein kombinierter Ablauf.

## 3. Dateien und Capture-Metadaten

### 3.1 Original-Capture (`usb.pcap`)

Beim erneuten Lesen des PCAP-Headers und der Record-Struktur wurden folgende Dateifakten bestätigt:

- Format: PCAP, Version 2.4
- Link-/Capture-Typ: USB mit USBPcap-Header
- Datei: `usb.pcap`
- Größe: 830.441 Bytes
- Anzahl Capture-Records: **14.944**
- Zeitspanne zwischen erstem und letztem Record: ca. **195,714 s**

Die Zahl 14.944 ist die Anzahl der Capture-Records. Sie ist nicht gleichbedeutend mit der Zahl vollständiger USB-Transfers oder HID-Reports.

### 3.2 Passthrough-Capture (`g815_test_passthrough.pcapng`)

Folgende Dateifakten wurden direkt aus der PCAPNG-Blockstruktur ermittelt:

- Format: PCAPNG
- Datei: `g815_test_passthrough.pcapng`
- Größe: **12.284 Bytes**
- Enhanced Packet Blocks / Captured Records: **148**
- Interface ID in den erfassten EPBs: **0**

Die frühere Auswertung gab eine Capture-Dauer von ca. 22,408 s an. Diese Dauer sollte bei Bedarf erneut aus den exakten Zeitstempeln der EPBs reproduziert werden.

### 3.3 Capture-Statistik im Vergleich

| Merkmal | Original | Passthrough |
|---|---:|---:|
| Dateiformat | PCAP | PCAPNG |
| Records | 14.944 | 148 |
| Capture-Größe | 830.441 Bytes | 12.284 Bytes |
| Dauer (früher ausgewertet) | 195,714 s | ca. 22,408 s |
| Capture-Interface | USBPcap | PCAPNG Interface 0 |
| Device Address | vorherige Analyse: überwiegend 17 | vorherige Analyse: 20 |


## 4. Geräteidentifikation und Device Address

Im Passthrough-Capture wurde zuvor Device Address **20 (dezimal)** bzw. `0x14` als konsistent verwendet identifiziert. Das ist als USB-Adresse plausibel, da Device Addresses vom Host bei der Enumeration dynamisch zugewiesen werden und sich zwischen Captures ändern können.

Der Passthrough-Capture enthält Descriptor-Daten, in denen zuvor die Identifikation `G815 RGB Mechanical` und Logitech-bezogene Angaben erkannt wurden. Damit ist die Zuordnung zum G815 plausibel. Für eine abschließende Identitätsbestätigung sollten VID, PID, Seriennummer (falls vorhanden), Device Descriptor sowie Interface-/Endpoint-Descriptor direkt aus der Wireshark-Dissektion dokumentiert werden.

**Device Address ist keine dauerhafte Geräteidentität.** Die Adressdifferenz zwischen Original (frühere Auswertung: 17) und Passthrough (20) ist für sich genommen kein Fehler.

## 5. Endpoints und Kommunikationsumfang – bisherige Analyse

Die folgenden Werte stammen aus der vorangegangenen Capture-Auswertung und sind als **noch zu reproduzierende Dissektionsergebnisse** gekennzeichnet, nicht als vollständig neu validierte Endstatistik.

| Endpoint / Bereich | Original (`usb.pcap`) | Passthrough | Vorläufige Beobachtung |
|---|---:|---:|---|
| Endpoint 0 / Control | 7.380 Records berichtet | 22 Records berichtet | Enumeration/Control-Transfers in beiden Captures vorhanden |
| Interrupt-IN `0x81` | 10 Records berichtet | 14 Records berichtet | Tastatur-/HID-Aktivität möglich; Interface-Zuordnung noch verifizieren |
| Interrupt-IN `0x82` | 7.448 Records berichtet | 2 Records, als leer berichtet | Auffällige Diskrepanz; Original enthält viel Aktivität, Passthrough kaum sichtbare Daten |
| 20-Byte Reports | 3.714 berichtet | 0 berichtet | Zentraler fehlender Datenstrom im Passthrough |
| 28-Byte SET_REPORT-artige Records | 3.684 berichtet | 12 berichtet | Vendor-SET_REPORT-Kommunikation im Passthrough deutlich reduziert |

Endpoint-Zählungen müssen nach Interface, Endpoint-Richtung, Transferart, URB-/Transferbezug und USBPcap-Record-Typ erneut kontrolliert werden. Insbesondere darf eine PCAP-Record-Zählung nicht ohne Dissektion als Anzahl vollständiger USB-Transfers interpretiert werden.

## 6. Report `0x11` – Hauptbefund aus der früheren Analyse

### 6.1 Original-Capture

Die vorherige Auswertung berichtete:

- Report-ID `0x11` als dominierende Report-Familie
- 3.714 Datensätze mit 20 Byte Payload (bisherige Zählung)
- 179 unterschiedliche Payload-Varianten (bisherige Zählung)
- wiederkehrendes Prefix `11 FF`

Beispiel-Payload, aus der früheren Auswertung übernommen:

```text
11 FF 10 5B 01 50 37 00 00 00 00 00 00 00 00 00 00 00 00 00
```

Weitere früher festgehaltene Varianten:

```text
11 FF 10 5B 27 B4 9B 00 00 00 00 00 00 00 00 00 00 00 00 00
11 FF 10 5B 26 99 D2 00 00 00 00 00 00 00 00 00 00 00 00 00
11 FF 10 7B 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

**Interpretation:** Die Bytes zeigen mehrere Payload-Zustände. Es ist noch nicht ausreichend belegt, welche Felder Farbe, Helligkeit, Effekt, Modus, Index, Kanal oder andere Parameter darstellen. Die Hex-Beispiele sind Rohdaten, keine bestätigte Felddekodierung.

### 6.2 Passthrough-Capture

Die frühere Auswertung berichtete **keine 20-Byte-Interrupt-Reports `0x11`** im Passthrough-Capture. Endpoint `0x82` wurde dabei als nur zwei Records ohne Nutzdaten beschrieben.

Das ist der auffälligste Unterschied, der weiter untersucht werden muss: Die im Original dominante Report-Familie ist in der Passthrough-Aufzeichnung nicht in der zuvor erwarteten Form sichtbar.

## 7. HID SET_REPORT

Die vorherige Auswertung identifizierte im Original 3.684 SET_REPORT-artige Transfers bzw. Records mit 28 Byte Capture-Länge. Im Passthrough-Capture wurden 12 solcher Records berichtet; sechs davon wurden in der früheren Detailauswertung als Vendor-SET_REPORT-Transfers hervorgehoben.

Beispiel eines zuvor interpretierten Setup-/Datenmusters:

```text
21 09 11 02 01 00 14 00 11 FF 10 7B ...
```

Diese Darstellung muss anhand der tatsächlichen USBPcap-Header- und Wireshark-Transferdissektion erneut validiert werden. Die genaue Zuordnung von `bmRequestType`, `bRequest`, `wValue`, `wIndex`, `wLength`, Report Type, Report-ID und Payload-Offset darf nicht ausschließlich aus einer zusammenhängenden Hex-Darstellung abgeleitet werden.

### 7.1 Sechs Transfers aus der früheren Passthrough-Auswertung

Die vorherige Analyse listete folgende Paketindex-/Payload-Variationen:

| Paketindex (frühere Auswertung) | Report-ID | Payload-Variation |
|---:|---|---|
| 84 | `0x11` | `00 1F` |
| 102 | `0x11` | `00 1A` |
| 128 | `0x11` | `00 1A` |
| 130 | `0x11` | `00 1F` |
| 144 | `0x11` | `00 1A` |
| 146 | `0x11` | `00 1F` |

Diese Paketindizes und gekürzten Payloads sind aus der vorangegangenen Analyse übernommen und in diesem Report **nicht unabhängig erneut verifiziert**. Sie sind Hinweise für die Nachprüfung in Wireshark, keine gesicherten Funktionszuordnungen. Insbesondere lässt sich daraus allein nicht folgern, dass Helligkeit, Farbe oder Effekte erfolgreich geändert wurden.

## 8. HID GET_REPORT – frühere Interpretation und offene Validierung

Die vorherige Auswertung beschrieb am Ende des Passthrough-Captures zwei GET_REPORT-Anfragen:

| Paketindex | Report-ID | Angeforderte Länge | frühere Beschreibung der Antwort |
|---:|---|---:|---|
| 140 | `0x11` | 20 Byte (`0x14`) | 1 Byte (`11`) |
| 142 | `0x12` | 64 Byte (`0x40`) | 1 Byte (`12`) |

Die Setup-Bytes wurden zuvor wie folgt angegeben:

```text
A1 01 11 01 01 00 14 00
A1 01 12 01 01 00 40 00
```

Die frühere Schlussfolgerung, dass die Antworten nur jeweils ein Byte lang seien, ist **noch nicht sicher bestätigt**. Capture-Records müssen anhand von USBPcap-Header, Transferphase (Setup/Data/Status), Richtung, URB-/Transfer-ID und Wireshark-Dissektion korrekt zusammengeführt werden. Ein Record, der nur ein Report-ID-Byte enthält, kann beispielsweise nur einen Teil eines Control-Transfers darstellen; die Capture-Record-Länge ist nicht automatisch die gesamte Transfer-Antwortlänge.

Die GET_REPORT-Antworten sind deshalb ein **wichtiger Prüfpunkt**, aber noch kein bestätigter Beleg für einen Gateway-Fehler.

## 9. Report-ID `0x12` – Korrektur der früheren Interpretation

Ein früherer Report hatte 18-Byte-Daten als HID-Report `0x12` interpretiert. Diese Zuordnung war fehlerhaft bzw. nicht belegt. Die Bytes können USB-Deskriptoren darstellen:

```text
12 01 00 02 00 00 00 40 6D 04 3F C3 02 31 01 02 03 01
```

Das Muster `12 01` ist typisch für einen 18-Byte USB Device Descriptor (bLength `0x12`, bDescriptorType `0x01`).

Ebenso:

```text
12 03 4C 00 6F 00 67 00 69 00 74 00 65 00 63 00 68 00
```

Das Muster `12 03` entspricht einem 18-Byte String Descriptor (Descriptor-Typ `0x03`); die UTF-16LE-artigen Daten beginnen mit Zeichen aus „Logitech“. Diese Beispiele sind **USB-Descriptor-Daten**, nicht automatisch HID-Reports mit Report-ID `0x12`.

Die tatsächliche HID-Report-ID `0x12` muss über den Report Descriptor und HID-Transfers (z. B. Setup-Request/Endpoint-Report) nachgewiesen werden.

## 10. Änderungen von Helligkeit, Farbe und Effekt

Der Testablauf enthält diese drei Änderungen in folgender Reihenfolge:

1. Helligkeit
2. Statische Farbe
3. Effekt

Da keine exakten Zeitmarken für den Beginn/Ende jeder Aktion vorliegen, können im kombinierten Capture keine Payloadänderungen mit ausreichender Sicherheit einzeln diesen Aktionen zugewiesen werden.

| Testaktion | Beobachtungsziel | Status der Zuordnung |
|---|---|---|
| Helligkeit | Parameteränderung im Vendor-Report | nicht isoliert bestätigt |
| Statische Farbe | RGB-/Farbparameter | nicht isoliert bestätigt |
| Effekt | Effekt-ID / Parameter / Geschwindigkeit | nicht isoliert bestätigt |

Hex-Bytes, die sich während des gesamten Captures verändern, dürfen nicht allein anhand ihrer Werte als RGB oder Helligkeit klassifiziert werden. Kontrollierte Einzelfunktions-Captures sind erforderlich.

## 11. M1/M2/M3, Game Mode und G-Tasten

Diese Funktionen wurden im Schritt 10 gemeinsam betätigt. Die frühere Auswertung meldete verschiedene Report-Präfixe/Varianten, konnte sie aber nicht sicher einzeln zuordnen.

| Funktion | Test im Original | Zuordnung im Capture |
|---|---|---|
| M1 | betätigt | nicht einzeln isoliert |
| M2 | betätigt | nicht einzeln isoliert |
| M3 | betätigt | nicht einzeln isoliert |
| Game Mode | betätigt | nicht einzeln isoliert |
| G-Tasten | betätigt | welche G-Tasten/Sequenzen im Einzelnen nicht eindeutig dokumentiert |

Es ist nicht gesichert, ob die beobachteten Reports Tastenevents, Profilwechsel, Zustandsabfragen, Konfigurationsübertragungen oder Kombinationen daraus darstellen.

## 12. Vergleich: Was ist vorhanden, was fehlt bzw. auffällig ist?

| Bereich | Original-Capture | Passthrough-Capture | Bewertung |
|---|---|---|---|
| USB-Enumeration | umfangreich vorhanden | vorhanden | grundsätzlich erfasst |
| Device Descriptor / Konfiguration | vorhanden | vorhanden | Identität und Struktur nochmals vollständig verifizieren |
| G815-Identifikation | erkannt | in früherer Dissektion erkannt | Gerätezuordnung plausibel |
| Device Address | frühere Analyse: 17 | frühere Analyse: 20 (`0x14`) | unterschiedliche Adresse ist normal möglich |
| Control-Transfers | umfangreich | vorhanden, deutlich weniger | Vergleich anhand Transfer-Sequenzen erforderlich |
| HID SET_REPORT | frühere Zählung 3.684 | frühere Zählung 12 | große Differenz, Zählmethode verifizieren |
| Report `0x11` – 20 Byte | frühere Zählung 3.714 | 0 berichtet | zentrale auffällige Abweichung |
| Interrupt-IN `0x82` | früher 7.448 Records berichtet | 2 Records, leer berichtet | sehr auffällig, erneut prüfen |
| Interrupt-IN `0x81` | früher 10 Records berichtet | früher 14 Records berichtet | Interpretation und Endpoint-Interface-Zuordnung prüfen |
| GET_REPORT | im Original nicht abschließend aufgelistet | zwei Requests früher beschrieben | Antwortlängen und Transfers korrekt zusammenführen |
| Helligkeitsänderung | Teil des kombinierten Tests | keine gesicherte äquivalente Aktion nachgewiesen | nicht verifiziert |
| Statische Farbe | Teil des kombinierten Tests | keine gesicherte äquivalente Aktion nachgewiesen | nicht verifiziert |
| Effekt | Teil des kombinierten Tests | keine gesicherte äquivalente Aktion nachgewiesen | nicht verifiziert |
| M1/M2/M3 | gemeinsam betätigt | nicht eindeutig belegt | nicht einzeln zugeordnet |
| Game Mode | gemeinsam betätigt | nicht eindeutig belegt | nicht einzeln zugeordnet |
| G-Tasten | gemeinsam betätigt | nicht eindeutig belegt | nicht einzeln zugeordnet |

**Interpretation:** Das Passthrough-Capture ist deutlich kleiner und enthält nicht die in der bisherigen Originalanalyse dominierenden 20-Byte-Reports. Diese Diskrepanz ist ein starker Anlass für eine gezielte Prüfung. Sie beweist jedoch allein noch nicht, an welcher Stelle Reports verloren gehen, ob beide Captures vergleichbare Aktionen enthalten oder ob die Capture-Filter/Interfaces identisch waren.

## 13. Was ist im bisherigen Vergleich falsch oder überinterpretiert?

### 13.1 Paket-/Record-Zahlen wurden nicht sauber abgegrenzt

- PCAP-Records, USBPcap-URB-Records, USB-Transfers und HID-Reports sind unterschiedliche Zähleinheiten.
- Die 14.944 Records des Originals und 148 Records des Passthroughs sind Dateifakten.
- Andere Werte (z. B. 3.714 Reports, 3.684 SET_REPORT, Endpoint-Counts) sind frühere Dissektionsergebnisse und müssen mit einem dokumentierten Filter/Parser reproduziert werden.

### 13.2 Report-ID `0x12` wurde mit Deskriptordaten verwechselt

18-Byte-Records mit Prefix `12 01` bzw. `12 03` entsprechen USB-Descriptor-Strukturen, nicht automatisch HID-Report-ID `0x12`. Diese frühere Interpretation ist zu korrigieren.

### 13.3 GET_REPORT-Antwortlängen sind nicht bestätigt

Die Aussage, GET_REPORT habe nur ein Byte geliefert, kann aus einem einzelnen Capture-Record nicht sicher abgeleitet werden, solange Control-Transfer-Phasen nicht zusammengeführt und Header-/Data-Offsets verifiziert sind.

### 13.4 RGB-/Helligkeits-/Effektfelder sind noch Hypothesen

Die Payloadvariationen belegen Unterschiede, aber keine semantische Feldzuordnung. Offsetbezeichnungen sollen bis zum kontrollierten Vergleich neutral bleiben (Byte 0, Byte 1 usw.).

### 13.5 Device Address ist kein Problembeweis

Die Adressänderung von 17 zu 20 ist im USB-Verhalten normal und kein Hinweis auf falsches Gerät oder Gateway-Fehler.

## 14. Mögliche Ursachen für die Capture-Differenz (Hypothesen, nicht Befunde)

Die deutlich kleinere Passthrough-Aufzeichnung kann mehrere Ursachen haben. Diese Punkte sind Prüfhypothesen und keine Feststellungen:

1. **Unterschiedlicher Testablauf:** Im Passthrough-Capture wurden eventuell nicht alle Originalaktionen in gleicher Weise oder Reihenfolge ausgeführt.
2. **G HUB-Kommunikation fehlt oder ist verkürzt:** G HUB könnte nicht vollständig initialisiert, das Gerät nicht erkannt oder nicht mit demselben Funktionsumfang gesteuert worden sein.
3. **USB-Transferweiterleitung:** Control-/Interrupt-Transfers, insbesondere Report-Responses oder Interrupt-IN, könnten nicht vollständig weitergeleitet bzw. vom Host nicht angefordert werden.
4. **Capture-Punkt oder Filter:** Captures könnten an unterschiedlichen Stellen der USB-Topologie, mit unterschiedlichem Filter oder mit abweichenden Interfaces erstellt worden sein.
5. **Endpoint-/Interface-Zuordnung:** Die Zuordnung von `0x81`/`0x82` muss anhand des vollständigen Descriptor-Sets überprüft werden; nicht alle Endpoints müssen im jeweiligen Test aktiv gepollt werden.
6. **Transferdarstellung / Record-Zuordnung:** Ein Control-Transfer kann in mehreren Capture-Records erscheinen. Setup-, Data- und Statusphase sind korrekt zuzuordnen, bevor Response-Längen beurteilt werden.

## 15. Priorisierte Folgeuntersuchung

### Priorität 1 – Capture-Korrektheit und Transferdissektion

- Beide Dateien in derselben Wireshark-Version öffnen.
- USBPcap-Dissektor und Capture-Interfaces kontrollieren.
- G815 über VID/PID und Descriptoren eindeutig identifizieren.
- Bus, Device Address, Interface, Endpoint, Richtung und Transferart tabellieren.
- Die vorherigen Endpunkt- und Report-Zählungen reproduzieren.
- Setup-, Data- und Statusphasen von Control-Transfers über Transfer-/URB-IDs zuordnen.

### Priorität 2 – Interrupt-IN `0x82`

- Im Original alle Report-Transfers auf `0x82` prüfen und Payload-Längen bestimmen.
- Im Passthrough alle Records auf `0x82` im Detail ansehen.
- Kontrollieren, ob der Host Polling-Requests stellt, ob der Gateway-Geräteseite Daten erhält und ob auf der PC-Seite tatsächlich Nutzdaten zurückkommen.
- Interface und Report Descriptor für Endpoint `0x82` eindeutig identifizieren.

### Priorität 3 – GET_REPORT

- GET_REPORT `0x11` und `0x12` als vollständige Control-Transfer-Sequenzen auswerten.
- Setup-Parameter (bmRequestType, bRequest, wValue, wIndex, wLength) aus der Dissektion exportieren.
- tatsächliche Antwortdatenlänge und Transferstatus bestimmen.
- Gegen das Original vergleichen.

### Priorität 4 – G HUB / Vendor SET_REPORT

- G HUB starten, Geräteerkennung abwarten und kontrollieren, ob der gleiche Kommunikationsumfang wie im Original entsteht.
- SET_REPORT-Transfers nach Request, Report Type, ID, Länge und Payload vergleichen.
- Wiederholungen, Zeitabstände und fehlende Request-/Response-Sequenzen dokumentieren.

### Priorität 5 – Kontrollierte Einzelaktionen

Neue Captures mit nur einer Änderung je Capture erstellen. Vor und nach jeder Aktion ausreichend Pause einhalten und Zeitmarke notieren:

- Helligkeit: niedrig → hoch → niedrig
- Farbe: nur Rot, dann nur Grün, nur Blau, Weiß; jeweils dokumentiert
- Effekt: Effekt A → B → A bei unveränderten übrigen Einstellungen
- M1, M2, M3: jede Taste separat
- Game Mode: Aus → Ein → Aus
- G1–G5: jede Taste einzeln, sofern vorhanden/zu testen
- G HUB Start/Ende: ohne sonstige Änderungen

## 16. Empfohlene Vergleichstabelle für die nächste Runde

Pro Transfer bzw. Report eine Zeile:

| Feld | Inhalt |
|---|---|
| Capture | Original / Passthrough |
| Timestamp | Zeitstempel |
| Packet/Frame | Paketnummer |
| Bus / Device | USB-Bus und Device Address |
| VID / PID | Gerätekennung |
| Interface | Interface-Nummer |
| Endpoint | Adresse und Richtung |
| Transfer type | Control / Interrupt / Bulk / Isochronous |
| URB/Transfer ID | Transfer-Zuordnung |
| Control Setup | bmRequestType, bRequest, wValue, wIndex, wLength |
| Report Type / ID | sofern zuverlässig dissektiert |
| Report length | tatsächliche Report-Datenlänge, ohne Capture-Header |
| Payload | vollständige Hex-Daten |
| Test phase | P0–P7 bzw. konkrete Aktion |
| Byte delta | geänderte Payload-Offsets gegenüber Baseline |
| Evidence | confirmed / likely / unknown |
| Notes | offene Fragen / Besonderheiten |

## 17. Relevanz für das USB-Gateway

Die gewünschte Architektur ist ein transparenter HID-Passthrough mit generischer Behandlung von HID-Geräten. Vendor-spezifische Daten müssen als Rohkommunikation erhalten bleiben, auch wenn ihre Semantik zunächst unbekannt ist.

Aus diesem Vergleich ergeben sich folgende Entwicklungsanforderungen:

- Enumeration und Descriptor-Weitergabe sind notwendige, aber nicht hinreichende Erfolgskriterien.
- HID-Report-ID, Report-Länge und Payload dürfen nicht auf Standard-Keyboard-Reports reduziert werden.
- Vendor-spezifische Control-Transfers (SET_REPORT/GET_REPORT) sind als vollständige Transaktionen zu behandeln.
- Interrupt-IN-Daten müssen korrekt von der Geräte- zur Hostseite weitergereicht werden.
- Report-Parsing soll zunächst passiv/raw erfolgen; keine Byte-Manipulation ohne bestätigte Feldzuordnung.
- Debug-Logging muss Transferphase, Endpoint, Richtung, Länge und Payload dokumentieren, ohne die Daten zu verändern.
- Die Tests sollten reproduzierbare Golden-Captures für Enumeration, G HUB Initialisierung, Beleuchtung, Effekte und Sondertasten bereitstellen.

## 18. Gesamtbewertung

**Device Address 20 (`0x14`) im Passthrough-Capture ist plausibel.** USB-Adressen sind dynamisch und müssen nicht mit der Originaladresse übereinstimmen. Descriptor-Daten machen die Identifikation des G815 plausibel; für den endgültigen Nachweis sollen VID/PID und vollständige Deskriptoren dokumentiert werden.

**Die Passthrough-Aufzeichnung ist wesentlich kleiner:** 148 Records gegenüber 14.944 im Original. Die frühere Analyse berichtet außerdem, dass die dominanten 20-Byte-Reports `0x11` und die Aktivität auf Endpoint `0x82` im Passthrough fehlen oder stark reduziert sind. Dies ist der zentrale technische Verdacht, der verifiziert werden muss.

**GET_REPORT ist noch nicht als fehlerhaft bewiesen.** Die zuvor berichteten 1-Byte-Antworten müssen anhand vollständiger Control-Transfer-Sequenzen geprüft werden.

**Der aktuelle Vergleich weist eine deutliche Capture-Abweichung aus, aber noch keine zweifelsfreie Fehlerlokalisierung.** Es ist noch nicht bewiesen, ob die Ursache im Gateway, im Testablauf, in G HUB, in der USB-Topologie oder in der Capture-/Dissektion liegt.

## 19. Kurzfassung für den Entwickler

1. Original `usb.pcap`: 14.944 PCAP-Records, ca. 195,714 s; vorherige Analyse meldete 3.714 20-Byte-Reports `0x11`, 3.684 SET_REPORT-artige Records und umfangreiche Endpoint-`0x82`-Aktivität.
2. Passthrough `g815_test_passthrough.pcapng`: 148 EPBs, 12.284 Bytes; Device Address 20 (`0x14`) plausibel, G815-Identifikation aus Descriptoren zuvor erkannt.
3. Frühere Analyse: 0 20-Byte-Reports `0x11` im Passthrough, nur zwei leere Endpoint-`0x82`-Records, stark reduzierte SET_REPORT-Aktivität.
4. Zwei GET_REPORT-Requests (IDs `0x11` und `0x12`) wurden zuvor berichtet; die tatsächlichen Antwortlängen müssen neu verifiziert werden.
5. Frühere 18-Byte-„Report 0x12“-Beispiele sind USB-Descriptoren (`12 01` Device Descriptor, `12 03` String Descriptor), nicht als HID-Reports zu klassifizieren.
6. Helligkeit, Farbe, Effekt und Sondertasten sind im Original in einem kombinierten Test erfolgt. Ohne Aktionszeitmarken sind keine sicheren Byte-Zuordnungen möglich.
7. Nächster Schritt: Wireshark-Transferdissektion und endpoint-/interface-genaue Gegenprüfung; danach kontrollierte Einzelaktions-Captures.

---


## Anhang A – Capture-Interpretationsregeln

- **Capture-Record:** Ein einzelner Eintrag in PCAP/PCAPNG.
- **USB-Transfer:** USB-Transaktion/Transfer, kann mehrere Records bzw. Phasen enthalten.
- **HID-Report:** HID-Nutzdaten gemäß Report Descriptor; Report-ID und Report-Länge anhand der Dissektion verifizieren.
- **Control-Transfer:** Setup, ggf. Data und Statusphase; einzelne Record-Längen sind nicht mit der Gesamtantwort gleichzusetzen.
- **Endpoint:** Adresse einschließlich Richtung; Interface-Zuordnung muss aus Deskriptoren hervorgehen.
- **Confirmed:** direkt durch Capture/Descriptor/isolierten Test belegt.
- **Previous finding:** aus früherer Auswertung übernommen, noch nicht erneut reproduziert.
- **Hypothesis:** plausible Erklärung, noch nicht belegt.

