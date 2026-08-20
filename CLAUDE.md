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
FQBN=esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=no_ota
arduino-cli compile --fqbn $FQBN                       # what CI runs
arduino-cli upload -p /dev/ttyUSB0 --fqbn $FQBN
openscad -o model.stl tiny_reader_2-13_case.scad        # what CI runs
```

- Both board options matter. `FlashSize=8M` because the board has 8MB and the `esp32s3` default
  is `4M`. `PartitionScheme=no_ota` does not pick the layout (`partitions.csv` overrides it), it
  picks the size guard — and `no_ota` declares exactly the 2MB our `app0` really is. A roomier
  scheme waves through a sketch that would overflow into the filesystem.
- Libraries live in `flake.nix` under `wrapArduinoCLI { libraries = ... }`, not vendored. Platform
  pinned to `2.0.10`.
- CI only checks that the sketch compiles and the SCAD renders: no host build, no committed test.
- `.pio/` and `.vscode/` are stale PlatformIO leftovers, gitignored. Not build config.

## Layout

`tiny-reader.ino` holds the state machine: `ScreenId`, transitions, the handlers `loop()`
dispatches to, and the sleep policy. The `src/` modules stay dumb:

- `Config.h` — the single tuning point: pins, timings, AP credentials, driver, battery scaling.
- `TextWrap.h` — word wrapping, header-only, free of Arduino/GxEPD2 so it compiles on a host.
- `Ui.cpp` — all GxEPD2 drawing, layouts, every screen. `Storage.cpp` — LittleFS books under
  `/books`, positions, stats. `Input.cpp` — `ButtonManager`. `WebPortal.cpp` — SoftAP, web
  server, and the upload page's converter JS.
- `FreeSerif9pt8b.h` — generated merged font, glyphs `0x20`–`0xFE` (latin-1). Do not hand-edit.
- `partitions.csv` — overrides the `PartitionScheme` option (`platform.txt` prebuild hooks give a
  sketch-folder CSV top priority). 2MB app, 5.88MB LittleFS, no OTA. **The data partition must
  stay labelled `spiffs`** — that is the label `LittleFS.begin()` mounts.

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
`consumeDirection()` returns net movement, so a burst costs one repaint.

**Refresh discipline.** The panel wants 1700ms full against 500ms partial. Turns are partial and
`partialsSinceFull` forces a full draw every `PARTIAL_REFRESH_LIMIT` *paints*, not presses, so
fast scrolling cannot skip past it. `showScreen()` always draws full. `Ok` in the read view is a
manual deep clean for sun ghosting. Nothing refreshes before deep sleep on purpose: waking
repaints fully anyway.

**Sleep.** Light sleep runs between turns with level-triggered GPIO wake, and has two traps:
`gpio_wakeup_enable()` overwrites the pin's interrupt type, so the falling edge must be restored
with `gpio_set_intr_type(pin, GPIO_INTR_NEGEDGE)` after every wake or a held button re-fires
forever; and GPIO and timer wake must be disabled before `esp_deep_sleep_start()`, which wakes
from `ext0` on `PIN_WAKE` alone. `millis()` keeps advancing across light sleep. Deep sleep after
`INACTIVITY_SLEEP_MS` hibernates the panel, and `sleepResumeMode` (`RTC_DATA_ATTR`) is the only
thing telling `setup()` whether to reopen the book. Both are suppressed while the portal is up.

**Handedness.** `Config::LEFT_HANDED` derives the logical pins and `DISPLAY_ROTATION`;
`Input.cpp` reads only those, never `PIN_BTN_*`. Note `PIN_OK` maps to the pin named
`PIN_BTN_HOME` and vice versa, and `PIN_WAKE` ignores the flag.

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
mappings and datasheets only.

## Conventions

Two-space indent, `static` file-local functions, `Config` constants in SCREAMING_CASE,
module-prefixed free functions (`storage*`, `ui*`, `webPortal*`) over classes. Arduino `String`
for text, `std::vector` for collections. Commit messages are a short imperative sentence. EUPL.
