# CLAUDE.md

Guidance for working in this repository.

## What this is

TinyReader: firmware plus 3D-printed enclosure for a pocketable e-ink e-reader on the Elecrow
CrowPanel ESP32-S3 2.13" board. Firmware is `tiny-reader.ino` with modules in `src/`; the
enclosure is one OpenSCAD file, `tiny_reader_2-13_case.scad`.

## Build

Everything comes from the Nix flake. `.envrc` is `use flake`, so direnv users already have it.

```sh
nix develop                                            # or rely on direnv
arduino-cli compile --fqbn esp32:esp32:esp32s3          # what CI runs
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32s3
openscad -o model.stl tiny_reader_2-13_case.scad        # what CI runs
```

- **No board options, deliberately.** Two files in the sketch root override the core instead,
  because `platform.txt` prefers both over anything it would build itself: `partitions.csv` and
  `bootloader.bin`. `app0` is sized to the stock `upload.maximum_size`, so the compile-time guard
  stays exact without touching the board menu.
- **`bootloader.bin` is why the 8MB layout boots, and it is generated, not committed.** The
  bootloader the core builds declares 4MB, and since that image carries a SHA-256 digest esptool
  refuses to correct the field while flashing — so a table spanning the board's real 8MB makes the
  bootloader reset before printing a single line. The flake regenerates it from the same ELF with
  `elf2image --flash_size 8MB` (`nix build .#bootloader`), and the devShell symlinks it in, so
  `nix develop` or direnv is enough. **If the screen stays blank and the serial log is nothing but
  repeating `rst:0x3 (RTC_SW_SYS_RST)` with no app output, this file is missing.**
- Libraries live in `flake.nix` under `wrapArduinoCLI { libraries = ... }`, not vendored. Platform
  pinned to `2.0.10`.
- CI only checks that the sketch compiles and the SCAD renders: no host build, no committed test.
- `.pio/` and `.vscode/` are stale PlatformIO leftovers, gitignored. Not build config.

## Layout

`tiny-reader.ino` holds the state machine: `ScreenId`, transitions, the handlers `loop()`
dispatches to, and the sleep policy. The `src/` modules stay dumb:

- `Config.h` — the single tuning point: pins, timings, AP credentials, panel revision, battery
  scaling. `EpdDriver`/`EpdDisplay` sit below the namespace, since they read `PANEL_JD79661`.
- `GxEPD2_213_JD79661.{h,cpp}` — GxEPD2 panel class for the second display module revision, the
  only first-party file that talks to a controller directly.
- `TextWrap.h` — word wrapping, header-only, free of Arduino/GxEPD2 so it compiles on a host.
- `Ui.cpp` — all GxEPD2 drawing, layouts, every screen. `Storage.cpp` — LittleFS books under
  `/books`, positions, stats. `Input.cpp` — `ButtonManager`. `WebPortal.cpp` — SoftAP, web
  server, and the upload page's converter JS.
- `FreeSerif9pt8b.h` — generated merged font, glyphs `0x20`–`0xFE` (latin-1). Do not hand-edit.
- `partitions.csv` — overrides the `PartitionScheme` option (`platform.txt` prebuild hooks give a
  sketch-folder CSV top priority). 1.25MB app, 6.62MB LittleFS, no OTA, 8MB total. **The data
  partition must stay labelled `spiffs`** — that is the label `LittleFS.begin()` mounts. Resizing
  it does not resize an existing filesystem: littlefs records its geometry in the superblock, so
  reformat (hold OK on the error screen) or write a fresh `mklittlefs -b 4096 -p 256` image.

## Invariants

**Pagination.** Pages are never precomputed: `readPageAt()` reads a 512-byte chunk into the
shared `pageBuffer`, and the count `TextWrap::wrapPage()` returns becomes `nextPagePos`.
`uiDrawReader` and `uiMeasurePage` share it, the latter drawing nothing so pages can be skipped
without a refresh. Three things `wrapPage` must keep doing or a book freezes on one page: accept
a word wider than the line rather than splitting it; never return 0 for non-empty input; count
trailing whitespace that produced no line as consumed. **Nothing guards these automatically** —
check a change with `g++ -std=c++11` and a stub `charWidth` first. That callback must return 0
for bytes the renderer skips (outside the font range, and `\r`). The sketch builds as `gnu++11`,
where default member initializers stop a struct being an aggregate: hence none on `Line`.

**Progress and history.** `/progress/<book-basename>.pos` holds `v1 <pos> <count> <history...>`
as decimal offsets; a bare integer is the older format and still loads. `/current.txt` holds the
last opened book. The path drops the extension, so same-named books in different folders collide.
`storageSavePosition` writes a `.tmp` and renames it over the record, because littlefs swaps
atomically and truncating the live file is how progress used to be lost on a brownout. History is
capped at `READER_HISTORY_MAX`, restored by `openBook` so `Prev` survives sleep, and entries past
EOF are dropped on load.

**Input.** Presses are counted in an `ARDUINO_ISR_ATTR` falling-edge handler, so none are lost
while the panel refreshes or a progress file is written. That handler must stay IRAM-resident (a
press can arrive while the flash cache is disabled), must call nothing from the Arduino HAL
(`digitalRead` is not IRAM-safe here), and debounces on `esp_timer_get_time()`; counters are
consumed under `portENTER_CRITICAL`. Polling only maintains `isDown()` for the format screen.
`consumeDirection()` returns net movement, so a burst costs one repaint. The handler watches
**both** edges and counts a press only once the contact has been **open** for the debounce
interval, tracked in `lastOpenUs`. Anything weaker double-counts: a window measured from the last
counted press lets the release chatter through, and so does a retriggerable window, because the
closing burst and the release burst are separated by however long the button was held. The level
is read straight from `GPIO.in` since `digitalRead` is not IRAM-safe; that assumes every button
pin is below GPIO32.

**Display module revisions.** Elecrow ships two different panels under the one 2.13" part number
and documents neither: an SSD1680Z one and a JD79661/EK79029 one, listed side by side as `Driver
Chip: SSD1680Z, JD79661` in the vendor readme, with a datasheet each. `Config::PANEL_JD79661`
selects between them and derives `EPD_RESET_DURATION_MS`; it feeds `EpdDriver` at the bottom of
`Config.h` through `std::conditional`, because a panel class is a type and cannot be picked by a
ternary. Pins, buttons and the visible 122x250 area are identical, so **the only symptom of a wrong
flag is a blank screen and `Busy Timeout!` on serial** — BUSY is active high on the SSD1680 and
active **low** on the JD79661, so a driver built for the other one waits ten seconds for an edge
that never comes. `src/GxEPD2_213_JD79661.{h,cpp}` is ours: the image path is GxEPD2's
`epd/GxEPD2_154_M09` (JD79653A, the b/w sibling) with the previous-frame plane moved from `0x26` to
`0x10` and the `0x91`/`0x92` partial-in/out pair dropped, and everything panel-specific comes from
Elecrow's `example/arduino-v1.2/main/EPD_Init.cpp`. The waveform is **downloaded from RAM** into
`0x20`–`0x24`, 56 bytes per register, and `_writeLut` swaps which of `0x22`/`0x23` receives which
table on alternate calls — that is VCOM balancing, not redundancy, so do not simplify it away.
Elecrow's `EPD_HW_Init_Fast`, `EPD_Update_Fast`, `EPD_PartUpdate` and `EPD_ALL_Fill` in that file
are unconverted SSD1680 leftovers still sending `0x22`/`0x20`/`0x3C`, three of them dead but
`EPD_ALL_Fill` still live in their own `clear_all()`: they are not a reference. Neither is
**GxEPD2's own JD79661 driver** — `epd4c/GxEPD2_213c_GDEY0213F51`, advertised in the library README
as `2.13" 4-color 122x250, JD79661`, is the obvious thing to reach for and is the wrong panel mode:
one 2-bit-per-pixel plane at `0x10`, no fast partial update, a 25s full refresh. Read it only for
the controller's opcodes (`0x83` partial window, `0x02`+`0x00` power off, BUSY low), and note the
two siblings disagree on both that window opcode and the length of `0x61` TRES — three bytes is the
b/w form, which is what Elecrow sends. **The DU partial path is ours, not vendor-proven:** Elecrow
ships the DU tables but never calls `lut_DU()`, so their firmware only ever full-refreshes.

**Refresh discipline.** The SSD1680 panel wants 1700ms full against 500ms partial, the JD79661 the
same 1700ms against a DU waveform its own table labels 300ms. Turns are partial and
`partialsSinceFull` forces a full draw every `PARTIAL_REFRESH_LIMIT` *paints*, not presses, so
fast scrolling cannot skip past it. `showScreen()` always draws full. `Ok` in the read view is a
manual deep clean for sun ghosting. Nothing refreshes before deep sleep on purpose: waking
repaints fully anyway. The leading `0` in `display.init(0, true, EPD_RESET_DURATION_MS, false)` is
not a bitrate: a nonzero one sets GxEPD2's `_diag_enabled` and every busy wait then prints a timing
line, burying the state log. The four-argument overload is there only for the reset duration. The sketch
owns `Serial.begin()`.

**The portal screen must not repaint on a timer.** `serviceWebPortal` compares the connected
station count and the uptime *in whole minutes* against `wifiShownClients` / `wifiShownMinutes`
and paints only on a change, which is why the on-screen uptime is minutes and not seconds. It
used to tick once a second, and a paint costs 600-800ms of loop time, so the panel was refreshing
for over half of the 15-minute `SERVER_TIMEOUT_MS` window. That starves `handleClient()` — the
only thing draining the socket, since `WebServer::_parseForm` reads a whole upload inside one
call — and keeps the panel charge pump loaded while clients are associating, on a LiPo where
brownout is an observed failure mode. It is *not* a CPU problem: the WiFi driver and lwIP are
pinned to core 0 (`CONFIG_ESP32_WIFI_TASK_PINNED_TO_CORE_0`,
`CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0`) while `loop()` runs on core 1, and GxEPD2's busy wait
yields via `vTaskDelay`. Do not move the server to its own task: that would share GxEPD2's SPI
across threads with no locking and fix nothing.

**Sleep.** Light sleep runs between page turns, and its wake collides with the press interrupt, so
`ButtonManager::prepareForLightSleep` / `resumeAfterLightSleep` own the whole dance — do not arm
GPIO wake from the sketch. Light sleep wakes on a *level*, and `gpio_wakeup_enable()` rewrites the
pin's trigger type to it, so a handler left attached across the wake re-fires for as long as the
button is held: it never reaches the code that would restore the edge, and the interrupt watchdog
panics the core (`Interrupt wdt timeout on CPU1`). Hence: detach handlers before arming the wake,
and on resume count a press for any pin still held — that falling edge happened with no handler
attached, and since light sleep sits between every press, dropping it would drop them all. Before
`esp_deep_sleep_start()`, clear everything with `esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)`
and then arm `ext0` on `PIN_WAKE`; disabling sources one at a time logs an error for each that is
already inactive. `millis()` keeps advancing across light sleep. Deep sleep after
`INACTIVITY_SLEEP_MS` hibernates the panel, and `sleepResumeMode` (`RTC_DATA_ATTR`) is the only
thing telling `setup()` whether to reopen the book. Both are suppressed while the portal is up.

**Handedness.** `Config::LEFT_HANDED` derives the logical pins and `DISPLAY_ROTATION`;
`Input.cpp` reads only those, never `PIN_BTN_*`. Note `PIN_OK` maps to the pin named
`PIN_BTN_HOME` and vice versa, and `PIN_WAKE` ignores the flag.

**The access point is configured explicitly, and its failures are reported.** `WiFi.persistent(false)`
first, because otherwise every start rewrites the config to NVS — a flash write, which disables
the flash cache and stalls non-IRAM code on *both* cores exactly while the AP comes up. Channel
and `max_connection` come from `Config`, and the returns of `softAP`, `softAPConfig` and
`server.begin()` are all checked: `webPortalStart` used to hardcode `return true`, so a dead AP
still displayed an SSID and password. A `DNSServer` resolving `*` plus an `onNotFound` redirect
make it a captive portal, so a client's connectivity probe gets an answer instead of timing out.
`onWifiEvent` logs join and leave; it takes `arduino_event_t*` because that is the only shape
`removeEvent()` also accepts, and IDF 4.4 carries no reason code on the AP disconnect event, so
the client's own supplicant log is the other half of any join diagnosis.

**Uploads size themselves once.** `freeSpace()` reaches `lfs_fs_size()`, a full metadata
traversal of the 6.62MB partition, so `handleUpload` measures `uploadBudget` at
`UPLOAD_FILE_START` and decrements it per chunk. Calling it per chunk meant ~360 traversals per
500KB book. `UPLOAD_FILE_ABORTED` must be handled or a truncated book is left in `/books`, and it
is guarded by `uploadInProgress`: an abort during the headers arrives with `uploadPath` still
naming the *previous* upload, which would otherwise be deleted.

**Book text is normalized before it arrives.** The device stores upload bytes verbatim: the
transliteration to latin-1, paragraph unwrap, hyphen rejoin and indent stripping all happen in
the upload page's JavaScript, using browser built-ins because the AP has no internet route. A
`curl` POST lands raw. Test it by extracting the `<script>` block and running `reflow`/`toLatin1`
under node.

**Font state.** The reader sets the custom GFX font and restores `setFont(nullptr)` plus
`setTextWrap(true)`; menu screens assume that.

**Serial logging is how this device is debugged.** Boot prints the reset reason (`brownout` means
the LiPo sagged, `panic` means a bug), `logState()` prints one line per screen change, and page
turns log `heap`/`maxalloc`. Keep them when editing transitions.

## Enclosure

One SCAD file, no includes. Measured dimensions are the constants between `--- BOARD
SPECIFICATIONS ---` and `--- ADJUSTMENT PARAMETERS ---`; adjust those, not numbers inside modules.
`ZERO_GAP` is nonzero only in `$preview` to avoid coincident-face artifacts, `EPS` pads hulls and
minkowski sums. `--- RENDER ---` lays out every printable part so one render gives the whole
plate. The `import("output.stl")` fit check stays commented out — that STL is gitignored, so the
file must render without it. `CrowPanel-ESP32-.../` is a gitignored vendor clone, for pin
mappings and datasheets only — including `Datasheet/EK79029DS-JD79661_Datasheet.pdf`, the other
half of any JD79661 register question.

## Conventions

Two-space indent, `static` file-local functions, `Config` constants in SCREAMING_CASE,
module-prefixed free functions (`storage*`, `ui*`, `webPortal*`) over classes. Arduino `String`
for text, `std::vector` for collections. Commit messages are a short imperative sentence. EUPL.
