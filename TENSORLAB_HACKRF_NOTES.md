# Tensor Lab HackRF / PortaPack Work — Notes and Handoff

Branch: `tensorlab` on `github.com/tensorlabresearch/mayhem-firmware`
Fork of `portapack-mayhem/mayhem-firmware`, branched from `next` @ `c4f32ac` (2026-07-25).

This is the working record of a Linux-workstation session: what was built, what
is proven on hardware, what is broken, and — most importantly — the platform
traps that cost real time to find. Read the **Gotchas** section before writing
any external app code. Several of these brick the device with a clean build and
no warning.

---

## 1. Hardware

- HackRF One **clone** + PortaPack. `hackrf_info` reports *"Hardware does not
  appear to have been manufactured by Great Scott Gadgets"*.
- Reports `Flash Allowed: 2MB` where a genuine HackRF One has 1 MB.
- **Bulk SPI-flash reads time out on this board.** Status-register reads work.
  So you cannot take a byte-level flash backup. Restore from a known image instead.
- Screen 240x320. `getdevtype` returns `PORTAPACK`.
- The project plan names a **PortaPack H4M**; this unit is not identifiably one.
  One firmware covers H1/H2/H2M/H4M, so it did not matter, but don't assume.
- External 10 MHz reference was connected throughout. Clock source is **boot-time
  autodetect**, not a setting — see Gotchas.

### Console access

Normal (PortaPack) mode presents a CDC-ACM serial console:

```
USB 1d50:6018
/dev/serial/by-id/usb-Great_Scott_Gadgets_PortaPack_Mayhem_Transceiver-if00
115200 baud (rate is ignored; it's USB bulk)
```

`hackrf` on that console switches to HackRF USB mode (`1d50:6089`); `reboot`
returns. In HackRF mode the console disappears.

The port alternates between `/dev/ttyACM0` and `ttyACM1` across reboots. **Always
resolve the `by-id` symlink.**

Reliable console commands: `info`, `ls`, `filesize`, `crc32`, `applist`,
`radioinfo`, `screenframeshort`, `button`, `touch`, `setfreq`, `reboot`, `hackrf`,
`sd_over_usb`.

`button N` is **1-based** over `enum Switch`:
`1=Right 2=Left 3=Down 4=Up 5=Sel` **`6=Dfu — avoid`**, `7`/`8` = encoder (−1/+1).
`OptionsField` values change with the **encoder** (7/8), not Left/Right.

The boot splash is dismissed by `on_touch` only — its `button_done` is a 1x1
widget parked off-screen at `x = screen_width`. Use `touch 120 150`; injecting
Sel does nothing.

### Screenshots without touching the device

`screenframeshort` dumps the framebuffer as 320 lines of 240 chars, RGB222 packed
as `32 + (r<<4 | g<<2 | b)`:

```python
n = ord(ch) - 32
r, g, b = (n >> 4) & 3, (n >> 2) & 3, n & 3   # scale by 85 for 8-bit
```

Only 2 bits per channel, so subtle colours (e.g. a dark red `{36,4,9}`) render as
pure black in captures. Don't judge theme work from these.

---

## 2. Build environment

```bash
docker build -f dockerfile-nogit -t tl-mayhem-build:noble .
docker run --rm -v "$PWD":/havoc tl-mayhem-build:noble ninja -j"$(nproc)"
```

- **Use `dockerfile-nogit`.** It pins `gcc-arm-none-eabi 9-2019-q4`. Mayhem is
  sensitive to toolchain version; a host 10.x/14.x will not do.
- **`dockerfile-alpine` and `docker-compose.yml` are dead.** The CMD still points
  at `/portapack-havoc/...` from the Havoc era, and `.dockerignore` is `*` so its
  `COPY` is a no-op.
- Build artifacts are **root-owned** (created in the container). `rm` as your user
  silently fails with permission errors — use `sudo`.
- Ninja does **not** delete outputs for de-registered apps, and the copy step
  re-stamps every `.ppma` each build, so removed apps look freshly built and get
  shipped. After removing apps, `sudo rm -rf build` and rebuild, then **count**
  the `.ppma`.
- `info`'s `Build time` is a `__DATE__`/`__TIME__` macro in a file incremental
  builds often skip. **It is not a build identifier.** Use `applist` or a
  `crc32` of the staged image.

### macOS continuation

Docker Desktop on Apple silicon runs the `ubuntu:noble` image under emulation;
the build works but is slow. `dockerfile-nogit-arm` exists in-tree and is the
better starting point — untested here.

You will need to rebuild the host tools (below) natively; `brew install dfu-util`
covers DFU.

---

## 3. Host tools — the shipped ones lie

Ubuntu's `hackrf 2021.03.1` against this firmware (API 1.11):
`hackrf_spiflash -r` returns **exit 0 while writing zero bytes**. It fails
silently. That cost an entire misdiagnosis.

Build matched tools from the in-tree submodule:

```bash
cmake /path/to/fresh_mayhem/hackrf/host -DCMAKE_BUILD_TYPE=Release && make -j
# needs libusb-1.0-dev, cmake, pkg-config
export LD_LIBRARY_PATH=<build>/libhackrf/src
```

These report real errors (`hackrf_spiflash_read() failed: Operation timed out`)
instead of pretending to succeed. Use them for anything that writes flash.

---

## 4. Flashing and recovery

Two routes:

**Console (no host tools needed)** — image must already be on the SD card:
```
flash /FIRMWARE/<image>.bin
```
Verify first with `crc32 <path>` against a host-computed **CRC-32/BZIP2**.

**Direct over USB** — switch to HackRF mode, then with the *matched* tools:
```
hackrf_spiflash -w portapack-mayhem-firmware.bin
hackrf_spiflash -R
```

### Recovery from a brick

The device was bricked and recovered **five times** during this work. The ROM DFU
bootloader cannot be bricked by a bad SPI image, so this always works:

1. Hold **DFU** while applying power → appears as `1fc9:000c`
2. `dfu-util --device 1fc9:000c --alt 0 --download hackrf_usb.dfu`
   → device becomes a working HackRF running **from RAM** (serial reads
   `RunningFromRAM`); SPI flash untouched
3. `hackrf_spiflash -w <good image>` then `-R`

Restore artifacts are kept outside the repo in `~/hackrf-flash-backups/`:
`stock-v2.4.0/firmware/firmware_hackrf.bin` (1 MB, correct `hackrf` variant) and
`hackrf_usb.dfu`. Re-download from the upstream v2.4.0 release if lost.

A **hang** (device still enumerates, console dead) only needs a power cycle. A
**brick** (no USB device at all) needs DFU. Learn the difference before panicking.

---

## 5. What was built

### Tensor Lab branding — done, on hardware

- `ThemeTensorLab` (ThemeId 6) from the brand palette: `#050203` ground,
  `#b61225`/`#ef233c` accents, `#fff5ef` ink.
- **Patched in BOTH theme implementations.** `firmware/standalone/common/ui/theme.cpp`
  is a parallel duplicate compiled into external `.ppma` apps, and its
  `SetTheme()` falls through to `ThemeDefault` on an unknown id. Patching only
  the application copy leaves every external app rendering in default grey.
  The two copies are **not source-compatible**: standalone declares fonts as
  *functions* (`font::fixed_8x16()`).
- `ok_dark`, `warning_dark`, `fg_green` and `status_active` are deliberately left
  conventionally coloured. This is a TX-capable radio; recolouring "good/active"
  to the same red used for errors and transmit would be unsafe branding.
- Boot splash: `/splash.bmp` must be **240x300** (not 240x320 — the status bar
  takes the top 20px), 24-bit uncompressed BI_RGB.

### TL Logo app — done, on hardware

`external/tl_logo/`. Renders the jellyfish watermark full-bleed via
`tl_common/ui_tl_backdrop.hpp`.

`TLBackdrop` is **header-only on purpose**: each external app links as its own
standalone binary collected by an object-name pattern in `external.ld`, so a
shared `.cpp` cannot be pulled into two different app sections.

`BMPFile` accepts 8/16/24/32 bpp with `compression == 0`; use **24-bit
uncompressed** (`convert ... BMP3:out.bmp`). `set_bg_color()` gives free colour
keying. Ship art on the SD card, not as flash arrays.

### RF Notebook — Milestone 2 done and validated; Milestone 3 broken

`external/rf_notebook/`. Manual capture per
`rf_field_notebook_project_plan.md` Milestone 2.

Writes `/RFNOTE/<session-id>/`:
```
session-device.json
events.jsonl              one JSON object per event
sketches/E000001.rfsk     1140 bytes each
```

**`.rfsk` format** (`rfsk.hpp`) — the plan's Level 1 evidence:
48-byte packed header + `avg[64]` + `matrix[16][64]` + CRC32 = **1140 bytes**.
`sizeof(Header)` is pinned by `static_assert` and mirrored in the importer.

Two deliberate signal-processing choices:
- 256 spectrum bins → 64 by **MAX, not mean**. A narrowband burst in one of four
  source bins survives as a peak instead of being averaged into noise.
- Noise floor is the **25th percentile** of the averaged spectrum, not the mean —
  a percentile resists being dragged up by the very signal being measured.

**Validated on hardware:**
- 100-mark stability run: 100/100 acked in 85 s, `event_seq` 1..100 contiguous,
  100 sketches all exactly 1140 bytes, **100/100 CRC valid**, 100/100 non-empty,
  identical JSON keys throughout, app still responsive. Plan §17 criterion met.
- Wi-Fi vs noise discrimination: on 2437 MHz the sketch shows clear vertical
  banding (persistent per-bin structure across all 16 frames) where an empty
  433 MHz band is uniform grain. Averaged-spectrum spread **82 vs 32**;
  `snr_spec` **50–62 vs 0**.

### `rfnotebook` importer — done

`tools/rfnotebook/rfnotebook.py` — `validate | render | export | report | all`.
Pure Python + Pillow. Runs anywhere, including the MacBook.

The important part: it **detects a sketch whose matrix is entirely zero**. That
failure CRCs perfectly and reads as success — it is exactly what the PCAP
baseband produced and what was initially mistaken for a working capture. Run
against that session it reports `CRC32 valid 3/3, non-empty matrices 0/3` and
exits nonzero.

Triage scoring is additive and prints its evidence rather than a bare number, per
plan §6's requirement that heuristics not be presented as fact.

### Receive-only build

39 apps removed from the build: every transmit app, the jam/spam apps
(`jammer`, `blespam`, `cvs_spam`), and the games. Required by plan §2.1 and §14,
and it removes the accidental-transmit hazard at a crowded conference. Sources
remain in tree; only the `EXTCPPSRC` entries and app-name list are commented out.

Flash headroom went 3164 → 22588 bytes as a side effect, which is what made room
for the app to grow at all.

---

## 6. Gotchas — read this before writing app code

### A namespace-scope object with a non-trivial constructor BRICKS THE DEVICE

```cpp
const std::filesystem::path rfnote_root{u"/RFNOTE"};   // namespace scope — BRICK
```

`std::filesystem::path` has a non-trivial constructor, so this emits a global
initialiser — visible in the symbol table as
`_GLOBAL__sub_I...` at an address **inside the app's fake region**. External apps
are loaded from SD as raw relocated blobs and never run their own C++ start-up,
so the firmware calls that constructor at boot into unmapped memory and dies
before USB enumerates. Total brick, DFU required.

Use `constexpr std::u16string_view` and build the path locally at point of use.

**The ELF has no `.init_array` section**, so grepping for one comes back empty and
looks like it clears the theory. Check for `_GLOBAL__sub_I` symbols:

```bash
readelf -sW build/firmware/application/application.elf | grep '_GLOBAL__sub_I.*<appname>'
```

Same hazard class: **function-local `static`s** need `__cxa_guard_acquire`. Keep
all storage as members.

*This single line cost ten flashes and five DFU recoveries to find.* A build-time
guard that greps for these symbols would turn it into a compile error and is
worth contributing upstream.

### An external app owning a baseband must start the M4 image itself

```cpp
baseband::run_prepared_image(portapack::memory::map::m4_code.base());  // in ctor
...
baseband::shutdown();                                                 // in dtor
```

Built-in apps get this for free. Without it, `receiver_model.enable()` and
`spectrum_streaming_start()` block on a baseband that was never running and the
UI thread hangs the instant the app opens. Pattern copied from `time_sink`.

### Choosing a baseband for `ChannelSpectrum`

Three attempts, and the reason matters:

| tag | result |
|---|---|
| `PCAP` (`proc_capture`) | has a `channel_spectrum` collector, but **`RecordView` is what feeds it** — frames arrive with `db[]` all zero |
| `PSPE` (wideband spectrum) | does not expose the collector through this path — **no frames at all** |
| `PWFM` (`proc_wfm_audio`) | **works.** Audio basebands self-configure for normal RX. WFM has the widest channel filter, so the best span |

`analogtv` (PAMT) and `time_sink` (PTSK) are the other working examples.

### `RxRadioState` takes the full signature

```cpp
RxRadioState radio_state_{freq, bandwidth, sample_rate, ReceiverModel::Mode::...};
```

Frequency `0` means "inherit whatever the radio is tuned to". Declare it early so
it is constructed first and destroyed last. Filter bandwidth must come from
`filter_bandwidth_for_sampling_rate()` — a raw `2'500'000` is not a valid
MAX2837 value.

### Adding an external app needs FOUR registrations

In `external/external.cmake`: the `EXTCPPSRC` source list **and** the app-name
list. In `external/external.ld`: a memory region **and** a `SECTIONS` block.
Missing the `.ld` entries fails at link, not configure.

Region addresses are link-time placeholders spaced `0x10000` apart, `len = 32k`.
`export_external_apps.py` rewrites them to real SRAM (`0x10080000 + m4_size`).

### `main.cpp` include order is load-bearing

`external_app.hpp` and `ui_navigation.hpp` include each other, and
`app_location_t` comes from `standalone_app.hpp` which `external_app.hpp` pulls
in only **after** `ui_navigation.hpp`. So `ui_navigation.hpp` must be seen first:

```cpp
#include "ui.hpp"
#include "ui_<app>.hpp"
#include "ui_navigation.hpp"
#include "external_app.hpp"      // last
```

Alphabetical ordering breaks the build.

### App size limit is 32 KB and cannot safely be raised

`maximum_application_size = 32*1024` in `firmware/tools/external_app_info.py`
also governs the relocation window, and the image is copied into real SRAM at
`0x10080000 + m4_size` (`m4_code` is 32 KiB at `0x10080000`). The `.ppma` is
**app code + baseband image**, so an 18.9 KB baseband leaves ~13.8 KB for code.

RF Notebook Milestone 2 is 27,924 bytes of the 32,768 budget.

### `ui::screen_width` is a runtime value

Multi-resolution support means it cannot size a `std::array`. Use a fixed maximum
(320) and clamp.

### CRC32 is CRC-32/BZIP2, not zlib

`CRC<32>{0x04c11db7, 0xffffffff, 0xffffffff}` is MSB-first, non-reflected —
matching Mayhem's `crc32` console command. zlib's reflected variant gives a
mismatch on perfectly good files. This produced a false alarm mid-flash.

### Upstream bug: `setfreq` cannot exceed 2.147 GHz

`cmd_setfreq` does `int64_t freq = atol(argv[0])`, but `atol` returns a **32-bit
`long`** on this target, so anything above 2,147,483,647 Hz saturates.
`setfreq 2437000000` tunes to 2147.4836 MHz. **The console cannot reach Wi-Fi,
Bluetooth or 5 GHz.** Needs `strtoll`. Not fixed — worth a PR.

Workaround: wire `FrequencyField::on_edit` to `FrequencyKeypadView` and type it.
Note `setfreq` only *broadcasts* `FreqChangeCommand` — an app must subscribe or
it silently does nothing.

### Other upstream staleness

- `external_app_info.py` had `external_apps_address_end = 0xAE0B0000`, behind
  `external.ld` (upstream's `tetra_rx` is at `0xAE0C0000`). Apps above the end
  were silently skipped by the leak scan. Raised to `0xAE200000` here.
- The "possible external code address" scan is **mostly false positives** —
  random 32-bit values in range. The *booting* build has hits at `0xAE0D`–`0xAE1E`.
  Don't chase them.
- `make_spi_image.py`'s application-size guard is dead code:
  `if len(image['data']) > image['size']` where `size` is set to `len(data)`.
  Nothing checks the application binary against a real limit.
- `usb_serial_asyncmsg.hpp`'s own header warns it is "not real async" and that
  concurrent transmissions corrupt each other. Do not build a protocol on it.

### Clock reference is boot-time autodetect

`ClockManager::choose_reference()` probes CLKIN and locks external only if the
measured frequency is 9.85–10.15 MHz; otherwise it falls back to the internal
crystal. There is **no setting, no UI toggle, no console command**. To go
internal: unplug the 10 MHz feed and power cycle — detection runs only at boot,
so a live unplug goes unnoticed. `info` reports the internal crystal as
**"HackRF"**, not "Internal".

### SD card workflow

`sd_over_usb` exposes the card as USB mass storage — no physical card handling
needed. Two caveats: it requires `/APPS/sdusb.ppma` to be present (its absence
made the command fail confusingly), and **exiting the mode requires a power
cycle**, so each app iteration costs two.

It also leaves the FAT dirty bit set. `fsck.vfat -n` reports no actual errors.
There are pre-existing duplicate directory entries from macOS `._*` files in
`/SUBGHZ` — not ours, and a repairing `fsck` would rename them.

### `VERSION_MD5` is a constant on an untagged branch

It is `md5(VERSION)` truncated to 8 hex chars, and `VERSION` resolves to the
literal string `"unknown"` — so it is `0xd9f7497c` for every build. External apps
load only when `VERSION_MD5 == app_version`, and **mismatches are skipped
silently** (`continue`, no message).

Consequences: our own `.ppma` stay "compatible" across rebuilds even when they
are not, and adding any external app changes `external.ld` and shifts **every**
other app's baked addresses. **Always redeploy the whole `/APPS` set together
with the firmware, never one app alone.** Tagging releases restores the check.

---

## 7. Current state

`HEAD` = `4ffec4b`, pushed to `origin/tensorlab`.

**Working and validated on hardware** (all at `56c4f84`):
- Firmware boots reliably
- Theme, splash, TL Logo
- RF Notebook Milestone 2 — 100-mark run clean, Wi-Fi discrimination proven
- `rfnotebook` importer

**Broken — Milestone 3 (`4ffec4b`)**: automatic detection **hangs the app on
launch**. Firmware is fine; a power cycle recovers. Debug state:

- Hang occurs **before `start_session()`** (no session directory created), i.e.
  during construction, before any detection logic runs — `auto_armed_` starts
  false so `evaluate_auto()` returns immediately.
- **Eliminated with evidence:** app size (22,680 bytes with 10 KB headroom still
  hangs — the PSPE test), SRAM overflow, baseband size, the `Checkbox` widget
  type (swapped to `Button`, still hangs), widget layout overlap, global
  constructors and guard variables (symbol table clean), section placement.
- **Remaining suspects:** the two new widgets (`text_auto`, `button_auto`) —
  adding a focusable widget changes Mayhem's geometric focus-navigation graph —
  or the `rfsk::Detector` member itself.
- **Next test, already built but never deployed:** the validated `56c4f84` app
  plus *only* the two widgets, no detector (28,040 bytes). Hangs → widgets/focus.
  Launches → the `Detector` member.

**Uncommitted working tree** holds that widgets-only bisect variant. `git
checkout 4ffec4b -- firmware/application/external/rf_notebook/` to get Milestone 3
back, or `56c4f84 --` for the last known-good app.

**Never implemented:** IQ capture (plan Level 2), SigMF export, scanning across a
range (Milestone 4 — blocked on the 32 KB ceiling), fox-hunt bearing/antenna
fields, gain/attenuation control or logging, companion device (deferred pending a
wifi module — which changes the transport away from USB Web Serial and makes the
plan's §10 protocol moot).

**Untested:** everything Milestone 3.

### Fox hunting

`foxhunt_rx` ("Fox hunt"), `level`, `detector_rx` and `fpv_detect` are all built
and on the card. **Use those, not RF Notebook.** RF Notebook's RSSI is a
cumulative min/avg/max that only resets on MARK — useless for sweeping an antenna
— and it has no gain or attenuator controls, which you need close to the fox.

---

## 8. Suggested next steps

1. Finish the Milestone 3 bisect (one variable per flash; the next build is ready).
2. If it is the focus graph: make `button_auto` non-focusable and toggle
   auto-detect another way, or reposition it.
3. Fix `snr`/`obw` semantics if they drift again — the current fields are
   `rssi_adc_raw` (RSSI ADC) versus `noise_floor_spec` / `noise_spread_spec` /
   `snr_spec` (spectrum domain). **Do not subtract across those two scales**; the
   original bug was exactly that, and it clamped SNR to zero forever.
4. Write `FIELD_TEST.md` and `KNOWN_LIMITATIONS.md` — plan §20 asks for both.
5. Upstream PRs worth offering: `strtoll` in `cmd_setfreq`; a build guard for
   `_GLOBAL__sub_I` in app sections; refresh the stale
   `external_apps_address_end`.
6. Milestone 4 (scanning) needs the 32 KB ceiling addressed first. Stepped
   retuning reuses the validated measurement chain; genuine wideband sweeping
   means reusing Looking Glass rather than reimplementing it.

## 9. Process lessons

- **Change one thing per flash.** Bundling four changes (shared header, app trim,
  address-range fix, new app) turned a one-line bug into ten flashes of guessing.
- **Verify the build before committing.** `efbdd79` was pushed without compiling.
- **A green CRC over a buffer of zeros proves nothing.** Validate contents, not
  just checksums — this is why the importer explicitly flags empty matrices.
- **Distinguish hang from brick** before asking for DFU.
- Read-side console commands are reliable. `appstart` and `fwb` both hard-wedged
  the device. Move files with `sd_over_usb` or a card reader, not over serial.
