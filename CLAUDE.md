# CLAUDE.md

Guidance for working in this repository.

## What this is

TinyReader: firmware + 3D-printed enclosure for a pocketable e-ink e-reader built on the
Elecrow CrowPanel ESP32-S3 2.13" e-paper board. Two deliverables live side by side:

- **Firmware** — Arduino sketch `tiny-reader.ino` plus modules in `src/`.
- **Enclosure** — a single OpenSCAD file `tiny_reader_2-13_case.scad`.

## Build

Everything comes from the Nix flake (arduino-cli with pinned ESP32 platform + libraries,
pyserial, openscad). `.envrc` is `use flake`, so direnv users already have it.

```sh
nix develop                                            # or rely on direnv
FQBN=esp32:esp32:esp32s3:FlashSize=8M,PartitionScheme=huge_app
arduino-cli compile --fqbn $FQBN                       # what CI runs
arduino-cli upload -p /dev/ttyUSB0 --fqbn $FQBN
openscad -o model.stl tiny_reader_2-13_case.scad        # what CI runs
```

The board options are not optional. `FlashSize=8M` is needed because the board has 8MB and the
stock `esp32s3` default is `4M`; `PartitionScheme=huge_app` only satisfies arduino-cli's
max-app-size accounting, since the real table comes from `partitions.csv` (see below). There are
no tests and no host-side build; CI (`.github/workflows/main.yml`) only checks that the sketch
compiles and the SCAD file renders.

The one piece of testable logic lives in the browser: the upload converter embedded in
`src/WebPortal.cpp`. It can be exercised outside the device by extracting the `<script>` block
and running `reflow` / `toLatin1` under node.

Arduino libraries are **not** vendored — they are declared in `flake.nix` under
`wrapArduinoCLI { libraries = ... }`. Adding a dependency means editing `flake.nix`, not a
`libraries/` folder or `platformio.ini`. The ESP32 platform is pinned to `2.0.10`.

`.pio/` and `.vscode/` are stale PlatformIO leftovers (gitignored, absolute paths to a
`tiny_reader` dir that no longer exists). Ignore them; do not treat them as build config.

## Firmware layout

`tiny-reader.ino` is the whole state machine: `ScreenId` enum, screen transitions, button
dispatch in `loop()`, deep-sleep policy. The `src/` modules are deliberately dumb and
stateless-ish:

- `src/Config.h` — the single tuning point: pin map, timings, Wi-Fi AP credentials, display
  driver class, battery ADC scaling. Prefer changing a constant here over hardcoding.
- `src/Ui.cpp/.h` — all GxEPD2 drawing. Owns two layouts (`uiLayout()` for menu screens,
  `uiReaderLayout()` for the reading view) and every screen's draw function.
- `src/Storage.cpp/.h` — LittleFS: book listing under `/books`, reading position files, stats.
- `src/Input.cpp/.h` — `ButtonManager`, debounce, short/long press with consume semantics.
- `src/WebPortal.cpp/.h` — SoftAP + `WebServer` on port 80 for uploading books.
- `src/FreeSerif9pt8b.h` — generated merged font, glyph range `0x20`–`0xFE` (latin-1). Do not
  hand-edit the bitmap tables.
- `partitions.csv` — overrides the selected `PartitionScheme` (`platform.txt` prebuild hooks give
  a sketch-folder CSV top priority). 2MB app, ~6.16MB LittleFS, no OTA. The data partition must
  stay labelled `spiffs`: that is the label `LittleFS.begin()` mounts.

## Non-obvious behaviour

**Pagination is a rendering side effect.** Pages are never precomputed. `readPage()` grabs a
fixed `Config::READ_BUFFER_SIZE` (512-byte) chunk, `uiDrawReader()` word-wraps as much of it as
fits and writes back how much it consumed via `const_cast<ReaderView&>(view).bytesConsumed`,
and `renderCurrentPage()` derives `nextPagePos` from that. Touching the wrap loop in `Ui.cpp`
changes where pages break. Keep the `bytesConsumed` contract intact.

**Back navigation is persisted.** `reader.history` is a vector capped at
`Config::READER_HISTORY_MAX`, saved with the position on every page turn and restored by
`openBook`, so `Prev` keeps working after deep sleep or a reboot. Entries pointing past the end
of the file are dropped on load, which is the guard against a book being replaced.

**Persistence format.** `/progress/<sanitized-book-name>.pos` holds one line,
`v1 <pos> <count> <history...>`, all decimal byte offsets; a bare integer is the pre-history
format and still loads. The last opened book path is in `/current.txt`. The progress path is
derived from the book's *basename* with the extension stripped, so two books with the same name
in different folders would collide.

**Saving progress must never truncate the live file.** `storageSavePosition` writes
`<name>.pos.tmp` and renames it over the record, because littlefs swaps the two atomically. The
previous code opened the real file with `"w"` on every page turn, so a brownout inside that
window left a 0-byte file and the book reopened at page one. Keep the tmp+rename shape.

**Deep sleep.** After `Config::INACTIVITY_SLEEP_MS` the device hibernates the panel and enters
deep sleep with EXT0 wake on `PIN_BTN_OK` only — no other button wakes it. `sleepResumeMode` is
`RTC_DATA_ATTR` and is the only thing that tells `setup()` whether to reopen the book or show
the menu. Sleep is suppressed while the web portal is up.

**E-paper refresh discipline.** Partial refreshes are counted and forced back to a full refresh
every `PARTIAL_REFRESH_LIMIT` pages (and `WIFI_SETTINGS_FULL_REFRESH_EVERY` on the Wi-Fi
screen) to clear ghosting. Screen entry via `showScreen()` always draws full.

**Font switching.** The reader sets the custom GFX font and restores `setFont(nullptr)` (the
built-in 5x7) plus `setTextWrap(true)` before returning. Menu screens assume that state.

**Books are normalized before they arrive.** The device stores upload bytes verbatim; all text
work (UTF-8 to latin-1 transliteration, paragraph unwrap, hyphen rejoining, indent stripping)
happens in the upload page's JavaScript, using only browser built-ins because the AP has no
internet route. A `curl` POST to `/upload` therefore bypasses normalization and lands raw.
`alreadyReflowed()` keeps a second conversion from collapsing paragraphs.

**Left- and right-handed builds share one switch.** `Config::LEFT_HANDED` derives the logical
pin constants (`PIN_OK`, `PIN_EXIT`, `PIN_PREV`, `PIN_NEXT`) and `DISPLAY_ROTATION`. `Input.cpp`
reads only the logical names, never the physical `PIN_BTN_*` ones. Note `PIN_OK` intentionally
maps to the pin named `PIN_BTN_HOME` and vice versa, and `PIN_WAKE` is a physical pin that the
flag does not touch.

**Serial logging is load-bearing for debugging.** `logState()` and the `BTN ...` prints at
115200 baud are the only way to observe the state machine on device; keep them when editing
transitions. Boot logs the reset reason (`brownout` vs `panic` distinguishes a sagging LiPo from
a firmware bug) and the page/state logs carry `heap=`/`maxalloc=` so fragmentation drift over a
long reading session is visible.

## Enclosure

One file, no includes. Hardware-measured dimensions are constants in the `--- BOARD
SPECIFICATIONS ---` .. `--- ADJUSTMENT PARAMETERS ---` block near the top — adjust those rather
than numbers buried in modules. `ZERO_GAP` is nonzero only in `$preview` to avoid coincident-face
artifacts; `EPS` is the small-but-nonzero padding for hulls and minkowski sums.

The bottom `--- RENDER ---` section instantiates all printable parts (`main_body`, `back_cover`,
two `tactile_button`s, `lever_button`) translated apart so a single render produces the full
plate. The board fit-check `import("output.stl")` at ~line 143 is intentionally commented out:
that STL is gitignored and absent in CI, so the file must always render without it.

The vendor reference directory `CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250/` is a
locally cloned Elecrow repo (datasheets, schematics, factory firmware and source). It is
gitignored and not part of the build — useful for pin mappings and panel behaviour only.

## Conventions

- C++: two-space indent, `static` file-local functions, `namespace Config` constants in
  SCREAMING_CASE, module-prefixed free functions (`storage*`, `ui*`, `webPortal*`) rather than
  classes. Arduino `String` throughout; `std::vector` for collections.
- Commit messages: short imperative sentence, no prefix convention.
- License is EUPL.
