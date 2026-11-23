# Technische Analyse: USBPcap-Mitschnitt (`g502_hero_test_direct_3.json`)

Diese Dokumentation fasst die vollständige Auswertung des Wireshark-/USBPcap-Exports (`g502_hero_test_direct_3.json`) zusammen. Der Fokus liegt auf der USB-Enumeration auf **Bus 2** sowie der Dekodierung der Interrupt-IN-Reports der **Logitech G502 HERO**.

---

## 1. Metadaten & Aufnahmeübersicht

| Eigenschaft | Wert |
| :--- | :--- |
| **Capture-Schnittstelle** | `\\.\USBPcap2` (USB Bus `2`) |
| **Startzeitpunkt (UTC)** | `2026-11-23T06:11:16.669255Z` |
| **Endzeitpunkt im Ausschnitt (UTC)** | `2026-11-23T06:11:16.699878Z` (Paket 123) |
| **Erfasste Zeitspanne** | ca. `30,62 ms` |
| **Enthaltene Pakete** | Paket `1` bis `123` (Datei bricht in Paket `123` ab) |
| **Hauptzielgerät** | Gerät `2.14` (`VID 0x046d`, `PID 0xc08b` – Logitech G502 HERO) |

### Paketstruktur im Mitschnitt
1. **Pakete `1` – `60`:** Initiale USB-Enumeration und Deskriptor-Abfragen (`GET_DESCRIPTOR` für Device, Configuration, String-Deskriptoren sowie `SET_CONFIGURATION`) für alle angeschlossenen Geräte an Bus `2`. USBPcap-Header-Länge: `28` Byte.
2. **Pakete `61` – `123`:** Aktiver Interrupt-IN-Verkehr (`usb.transfer_type: "0x01"`), überwiegend von Endpunkt `0x81` des Geräts `2.14.1` (Logitech G502 HERO) sowie einem einzelnen Interrupt-IN-Paket von Gerät `2.3.1` (Paket `97`). USBPcap-Header-Länge: `27` Byte.

---

## 2. Topologie der erkannten USB-Geräte (Bus 2)

Während der Enumerationsphase (Pakete `1`–`60`) wurden insgesamt **10 USB-Geräte** auf Bus `2` identifiziert:

| Geräteadresse | Vendor ID (`idVendor`) | Product ID (`idProduct`) | USB-Version (`bcdUSB`) | Klasse / Typ | Beschreibung & Besonderheiten |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`2.1`** | `0x051d` | `0x0002` | `0x0200` (USB 2.0) | HID (`0x03`) | 1 Interface, Endpoint `0x81` (`wMaxPacketSize: 8`, `bInterval: 10`), HID-Report-Descriptor: `1029` Byte |
| **`2.2`** | `0x046d` | `0x0a9c` | `0x0200` (USB 2.0) | Audio (`0x01`) + HID (`0x03`) | Logitech Composite-Gerät (4 Interfaces: Audio Control, Audio Streaming Out/In, HID mit Endpoint `0x83`) |
| **`2.3`** | `0x04e6` | `0x5113` | `0x0110` (USB 1.1) | Smart Card / CCID (`0x0b`) | 1 Interface, Endpoints `0x81` (Interrupt IN, `bInterval: 24`), `0x02` (Bulk OUT), `0x82` (Bulk IN) |
| **`2.4`** | `0x058f` | `0x6366` | `0x0200` (USB 2.0) | Mass Storage (`0x08`) | 1 Interface, Bulk-Endpoints `0x01` (OUT) & `0x82` (IN), `wMaxPacketSize: 512` |
| **`2.5`** | `0x05e3` | `0x0620` | `0x0310` (USB 3.1) | Hub (`0x09`) | Genesys Logic USB 3.1 Hub (`bDeviceProtocol: 3`, Endpoint `0x81`, `bInterval: 12`) |
| **`2.6`** | `0x05e3` | `0x0610` | `0x0210` (USB 2.1) | Hub (`0x09`) | Genesys Logic USB 2.1 Companion Hub (High-Speed TT, Endpoint `0x81`, `bInterval: 12`) |
| **`2.7`** | `0x0b05` | `0x190e` | `0x0200` (USB 2.0) | Wireless (`0xe0/0x01/0x01`) | ASUS Bluetooth/Wireless Adapter (2 Interfaces, Interrupt `0x81`, Bulk `0x02`/`0x82`, Isochronous `0x03`/`0x83`) |
| **`2.8`** | `0x0b05` | `0x18da` | `0x0200` (USB 2.0) | HID (`0x03`) | ASUS HID-Controller, Endpoints `0x82` (IN) & `0x03` (OUT), `wMaxPacketSize: 64`, `bInterval: 1` (1000 Hz) |
| **`2.10`** | `0x046d` | `0xc33f` | `0x0200` (USB 2.0) | HID (`0x03`) | **Logitech G815 Tastatur** (2 Interfaces: Boot Keyboard `0x81` + Vendor/Media HID `0x82`, beide `bInterval: 1`) |
| **`2.14`** | `0x046d` | `0xc08b` | `0x0200` (USB 2.0) | HID (`0x03`) | **Logitech G502 HERO Gaming-Maus** (Hauptzielgerät dieses Mitschnitts) |

---

## 3. Detailanalyse: Logitech G502 HERO (`2.14`)

### 3.1 Device Descriptor (`Pakete 53 & 54`)
* **Vendor ID:** `0x046d` (Logitech Inc.)
* **Product ID:** `0xc08b` (G502 HERO Gaming Mouse)
* **USB-Spezifikation (`bcdUSB`):** `0x0200` (USB 2.0 Full-Speed)
* **Device Release (`bcdDevice`):** `0x2703` (Firmware-Version `27.03`)
* **Max. Paketgröße EP0 (`bMaxPacketSize0`):** `64` Byte
* **Anzahl Konfigurationen (`bNumConfigurations`):** `1`
* **String-Indizes:** Manufacturer = `1`, Product = `2`, SerialNumber = `3`

### 3.2 Configuration & Interface Descriptors (`Pakete 55 & 56`)
* **Konfigurationswert (`bConfigurationValue`):** `1`
* **Stromversorgung (`bmAttributes` / `bMaxPower`):** Bus-Powered (`0xa0`, Remote Wakeup unterstützt), max. Stromaufnahme `150 * 2 mA = 300 mA`
* **Anzahl Interfaces (`bNumInterfaces`):** `2`

#### Interface `0` (`2.14.1`) – Primäre Mausdaten (Boot-kompatibel + 16-Bit-Achsen)
* **Interface-Klasse:** `0x03` (HID – Human Interface Device)
* **Subklasse:** `0x01` (Boot Interface Subclass)
* **Protokoll:** `0x02` (Mouse)
* **HID-Spezifikation (`bcdHID`):** `0x0111` (HID 1.11)
* **Report-Descriptor-Länge:** `67` Byte
* **Endpoint:** `0x81` (IN, Interrupt-Transfer `0x03`)
  * `wMaxPacketSize`: `8` Byte
  * `bInterval`: `1` (entspricht **1 ms / 1000 Hz Polling-Rate**)

#### Interface `1` (`2.14.2`) – Sondertasten, Makros & Logitech HID++
* **Interface-Klasse:** `0x03` (HID)
* **Subklasse:** `0x00` (No Subclass)
* **Protokoll:** `0x00` (None)
* **HID-Spezifikation (`bcdHID`):** `0x0111` (HID 1.11)
* **Report-Descriptor-Länge:** `151` Byte
* **Endpoint:** `0x82` (IN, Interrupt-Transfer `0x03`)
  * `wMaxPacketSize`: `20` Byte
  * `bInterval`: `1` (1000 Hz)

---

## 4. Dekodierung der HID-Maus-Reports (`usbhid.data`)

Die Logitech G502 HERO sendet über Endpoint `0x81` ein **8-Byte-Datenpaket** mit **16-Bit Little-Endian Vorzeichenwerten (Two's Complement)** für die X- und Y-Achse. Dadurch sind hochauflösende Sensorbewegungen ohne Überlauf bei schnellen Bewegungen möglich.

### 4.1 Byte-Layout des 8-Byte-Reports

| Byte-Offset | Hex-Beispiel | Datentyp | Funktion | Beschreibung |
| :--- | :--- | :--- | :--- | :--- |
| **Byte `0`** | `0x00` | `uint8` (Bitmaske) | **Maustasten** | Bit 0: Links, Bit 1: Rechts, Bit 2: Mitte, Bit 3: Zurück, Bit 4: Vorwärts |
| **Byte `1`** | `0x00` | `uint8` | **Reserviert / Zusatz** | Im gesamten Log `0x00` |
| **Byte `2–3`** | `0x01 0x00` | `int16_le` | **X-Achse ($\Delta X$)** | Relative Horizontalbewegung (`0x0000` = `0`, `0x0001` = `+1` nach rechts) |
| **Byte `4–5`** | `0xfe 0xff` | `int16_le` | **Y-Achse ($\Delta Y$)** | Relative Vertikalbewegung (`0xffff` = `-1`, `0xfffe` = `-2`, `0xfffd` = `-3` nach oben) |
| **Byte `6`** | `0x00` | `int8` | **Vertikales Mausrad** | Scrollrad-Schritte (positiv = hoch, negativ = runter) |
| **Byte `7`** | `0x00` | `int8` | **Horizontales Mausrad** | Tilt-Wheel / AC Pan (links/rechts) |

> **Hinweis zur Umrechnung (Two's Complement, 16-Bit Little-Endian):**
> Ein Bytepaar `[b_low, b_high]` ergibt den vorzeichenlosen 16-Bit-Wert $u = b_{\text{low}} + 256 \cdot b_{\text{high}}$. Ist $u \ge 32768$, lautet der vorzeichenbehaftete Wert $s = u - 65536$.
> * Beispiel `fe:ff`: $u = 254 + 256 \cdot 255 = 65534 \Rightarrow s = 65534 - 65536 = -2$.
> * Beispiel `01:00`: $u = 1 + 256 \cdot 0 = 1 \Rightarrow s = +1$.

---

## 5. Zeitreihen- & Bewegungsauswertung (Pakete `61`–`123`)

### 5.1 Polling-Stabilität & Timing
* **Erster Report (Paket `61`):** `06:11:16.669886Z`
* **Letzter vollständiger Report (Paket `121`):** `06:11:16.699875Z`
* **Anzahl erfasster Maus-Reports:** `31` Reports (jeweils URB-Submit + URB-Complete-Paar)
* **Zeitabstand zwischen den Reports ($\Delta t$):** Durchgehend `0,978 ms` bis `1,005 ms` (Mittelwert: **`0,9996 ms`**)
* **Effektive Abtastrate:** **`1000,4 Hz`** — der USB-Controller fragt die Maus extrem präzise im 1-ms-Raster ab, ohne Jitter-Ausreißer oder Paketverluste.

### 5.2 Tabellarischer Verlauf der dekodierten HID-Reports

| Paket | Zeitstempel (UTC) | $\Delta t$ zum vorh. Report | Raw Payload (`usbhid.data`) | Tasten | $\Delta X$ | $\Delta Y$ | Rad / Pan |
| :--- | :--- | :--- | :--- | :---: | :---: | :---: | :---: |
| **61** | `06:11:16.669886Z` | – | `00:00:00:00:ff:ff:00:00` | `0` | `0` | `-1` | `0 / 0` |
| **63** | `06:11:16.670891Z` | `1,005 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **65** | `06:11:16.671892Z` | `1,001 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **67** | `06:11:16.672893Z` | `1,001 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **69** | `06:11:16.673893Z` | `1,000 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **71** | `06:11:16.674896Z` | `1,003 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **73** | `06:11:16.675898Z` | `1,002 ms` | `00:00:00:00:ff:ff:00:00` | `0` | `0` | `-1` | `0 / 0` |
| **75** | `06:11:16.676897Z` | `0,999 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **77** | `06:11:16.677895Z` | `0,998 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **79** | `06:11:16.678896Z` | `1,001 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **81** | `06:11:16.679893Z` | `0,997 ms` | `00:00:00:00:ff:ff:00:00` | `0` | `0` | `-1` | `0 / 0` |
| **83** | `06:11:16.680891Z` | `0,998 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **85** | `06:11:16.681891Z` | `1,000 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **87** | `06:11:16.682891Z` | `1,000 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **89** | `06:11:16.683891Z` | `1,000 ms` | `00:00:01:00:fe:ff:00:00` | `0` | `+1` | `-2` | `0 / 0` |
| **91** | `06:11:16.684888Z` | `0,997 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **93** | `06:11:16.685888Z` | `1,000 ms` | `00:00:01:00:fe:ff:00:00` | `0` | `+1` | `-2` | `0 / 0` |
| **95** | `06:11:16.686889Z` | `1,001 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **99** | `06:11:16.687883Z` | `0,994 ms` | `00:00:00:00:fd:ff:00:00` | `0` | `0` | `-3` | `0 / 0` |
| **101** | `06:11:16.688885Z` | `1,002 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **103** | `06:11:16.689885Z` | `1,000 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **105** | `06:11:16.690886Z` | `1,001 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **107** | `06:11:16.691882Z` | `0,996 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **109** | `06:11:16.692882Z` | `1,000 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **111** | `06:11:16.693879Z` | `0,997 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **113** | `06:11:16.694879Z` | `1,000 ms` | `00:00:00:00:fd:ff:00:00` | `0` | `0` | `-3` | `0 / 0` |
| **115** | `06:11:16.695880Z` | `1,001 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **117** | `06:11:16.696877Z` | `0,997 ms` | `00:00:01:00:fe:ff:00:00` | `0` | `+1` | `-2` | `0 / 0` |
| **119** | `06:11:16.697876Z` | `0,999 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |
| **121** | `06:11:16.699875Z` | `1,999 ms` | `00:00:00:00:fe:ff:00:00` | `0` | `0` | `-2` | `0 / 0` |

### 5.3 Kumulative Bewegungsbilanz (30 ms Fenster)
* **Gesamtverschiebung $\sum \Delta X$:** `+3 Counts` (leicht nach rechts)
* **Gesamtverschiebung $\sum \Delta Y$:** `-59 Counts` (gleichmäßige Bewegung nach oben)
* **Bewegungswinkel:** $\arctan2(-59, 3) \approx -87,1^\circ$ (nahezu vertikale Aufwärtsbewegung mit minimaler Rechtsdrift von ca. $2,9^\circ$)
* **Durchschnittliche Geschwindigkeit:** $\approx 1969\text{ Counts/s}$ (entspricht bei 800 DPI ca. $6,25\text{ cm/s}$, bei 1600 DPI ca. $3,13\text{ cm/s}$)

---

## 6. Weitere Beobachtungen & Auffälligkeiten

1. **Hintergrund-Traffic von Gerät `2.3.1` (Smart Card Reader):**
   * In Paket `97` (`06:11:16.687312Z`) sendet das CCID-Gerät `2.3.1` (`VID 0x04e6`, `PID 0x5113`, Endpoint `0x81`) einen 4-Byte-Interrupt-Payload (`0c:64:6c:03`). Dies beeinträchtigt die Polling-Latenz der G502 HERO jedoch nicht (`Paket 95` $\rightarrow$ `Paket 99`: `0,994 ms`).
2. **Kein Traffic auf Interface 1 (`2.14.2`, Endpoint `0x82`):**
   * Da im erfassten Zeitraum keine Makrotasten betätigt wurden und keine Logitech G HUB / Onboard-Memory-Manager-Konfigurationspakete (HID++) liefen, blieb Endpoint `0x82` inaktiv.
3. **Unvollständiges Dateiende (`Paket 123`):**
   * Die JSON-Datei bricht innerhalb von Paket `123` (URB-Submit für den nächsten Interrupt-IN-Transfer von `2.14.1` bei `06:11:16.699878Z`) mitten im Key `"usb.time":` ab. Für automatisierte Parser empfiehlt es sich, unvollständige JSON-Objekte am Ende abzufangen oder den Export in Wireshark erneut vollständig durchzuführen.