# Agent Handoff — Tensor Lab PortaPack work

You are picking up firmware work on a HackRF One + PortaPack running a forked
Mayhem. The device is connected to this laptop (macOS). Previous work was done on
a Linux workstation.

**Read these three files before touching anything:**

1. `TENSORLAB_HACKRF_NOTES.md` — platform traps. **Several brick the device with
   a clean build and no warning.** This is not optional reading.
2. `TENSORLAB_MODULES.md` — the four hunt/monitor modules and their feasibility
   limits.
3. `TENSORLAB_ROADMAP.md` — what's next and why, including the MDK and companion
   app design.

Repo: `github.com/tensorlabresearch/mayhem-firmware`, branch **`tensorlab`**.
Push there freely; it is the intended home for this work.

---

## 0. Five rules, learned expensively

1. **One variable per flash.** A previous session bundled four changes and turned
   a one-line bug into ten flashes and five DFU recoveries. Change one thing,
   build, flash, observe.
2. **Verify the build before you commit.** Commit `efbdd79` was pushed without
   compiling because `git add` ran unconditionally. Gate it on the build result.
3. **Run the brick guard before every flash** (§4). A single namespace-scope
   `std::filesystem::path` bricked the device; the guard catches that class of
   fault statically in one second.
4. **A green checksum proves nothing about contents.** A `.rfsk` file once passed
   CRC perfectly while being entirely zeros. Validate data, not just integrity.
5. **Distinguish hang from brick before asking the operator for anything.**
   Device still enumerates + console dead = **hang**, power cycle fixes it.
   No USB device at all = **brick**, needs DFU. Getting this wrong wastes the
   operator's time.

---

## 1. Environment setup (macOS)

```bash
brew install dfu-util libusb cmake pkg-config
pip install pyserial pillow
```

**Build** — Docker only; do not try a native toolchain. Mayhem is sensitive to
compiler version and `dockerfile-nogit` pins `gcc-arm-none-eabi 9-2019-q4`.

```bash
docker build -f dockerfile-nogit -t tl-mayhem-build:noble .
docker run --rm -v "$PWD":/havoc tl-mayhem-build:noble ninja -j"$(sysctl -n hw.ncpu)"
```

On Apple silicon that image runs emulated and is slow. **`dockerfile-nogit-arm`
exists in-tree and is the better starting point — untested, try it early.**

Do **not** use `dockerfile-alpine` or `docker-compose.yml`; both are dead
(their CMD still references the Havoc era, and `.dockerignore` is `*`).

**Host HackRF tools — build them, don't install them.** Any packaged `hackrf`
older than this firmware returns *exit 0 while writing zero bytes* on flash
reads. It fails silently and will mislead you.

```bash
cmake hackrf/host -B /tmp/hackrf-host -DCMAKE_BUILD_TYPE=Release
make -C /tmp/hackrf-host -j
export DYLD_LIBRARY_PATH=/tmp/hackrf-host/libhackrf/src   # macOS
```

### macOS differences from the notes

The notes were written on Linux. What changes:

| Linux | macOS |
|---|---|
| `/dev/serial/by-id/...` → `ttyACM0/1` | `/dev/cu.usbmodem*` — **`cu.` not `tty.`**; opening `tty.*` blocks on carrier detect and looks like a hang |
| `lsusb` | `system_profiler SPUSBDataType \| grep -i -A3 'hackrf\|portapack'` |
| `udisksctl mount -b /dev/sdd` | auto-mounts to `/Volumes/<label>`; use `diskutil list` / `diskutil unmountDisk` |
| `sudo -n` was configured | may prompt; ask the operator rather than assuming |

`tools/tl_devtools/ppctl.py` handles port resolution for both platforms.

---

## 2. Talking to the device

```bash
python3 tools/tl_devtools/ppctl.py port          # resolved device path
python3 tools/tl_devtools/ppctl.py info          # firmware identity
python3 tools/tl_devtools/ppctl.py splash        # dismiss boot splash
python3 tools/tl_devtools/ppctl.py shot s.png    # framebuffer screenshot
python3 tools/tl_devtools/ppctl.py btn 3 3 5     # Down, Down, Select
python3 tools/tl_devtools/ppctl.py cmd "ls /RFNOTE"
```

**The console is your eyes.** There is no debugger. Read-side commands (`info`,
`ls`, `filesize`, `crc32`, `applist`, `screenframeshort`, `button`, `touch`) are
reliable. `appstart` and `fwb` both **hard-wedged** the device in prior sessions —
avoid them.

Switch numbering is 1-based: `1=Right 2=Left 3=Down 4=Up 5=Sel` — **`6` is DFU,
never send it** (`ppctl` refuses). `7`/`8` are the encoder, and `OptionsField`
values only change with the encoder, not Left/Right.

The boot splash is dismissed by **touch only**; injecting Select does nothing.

### Moving files to the SD card

`sd_over_usb` exposes the card as USB mass storage — no physical card handling.
Two catches: it needs `/APPS/sdusb.ppma` present, and **exiting the mode requires
a power cycle**, so each app iteration costs two operator power cycles. On macOS
the volume auto-mounts under `/Volumes/`.

---

## 3. Flashing and recovery

**Do not flash without the operator's go-ahead.** It is a hard-to-reverse write
and recovery needs them to physically hold the DFU button.

Two routes:

```bash
# A: via console, image must already be on the SD card
python3 tools/tl_devtools/ppctl.py cmd "flash /FIRMWARE/<image>.bin"

# B: direct over USB -- switch to HackRF mode first
python3 tools/tl_devtools/ppctl.py cmd "hackrf"     # port will drop; expected
/tmp/hackrf-host/hackrf-tools/src/hackrf_spiflash -w build/firmware/portapack-mayhem-firmware.bin
/tmp/hackrf-host/hackrf-tools/src/hackrf_spiflash -R
```

Verify before writing. `crc32` on the device is **CRC-32/BZIP2** (poly
0x04C11DB7, MSB-first) — *not* zlib's reflected variant. Comparing against zlib
gives a false mismatch on a perfectly good file; that already caused one scare.

### Brick recovery — always works

The ROM DFU bootloader cannot be bricked by a bad SPI image.

1. Ask the operator to **hold DFU while applying power** → `1fc9:000c`
2. `dfu-util --device 1fc9:000c --alt 0 --download hackrf_usb.dfu`
   → device runs a working HackRF **from RAM**, flash untouched
   (serial reads `RunningFromRAM`)
3. `hackrf_spiflash -w <known-good image>` then `-R`

Restore images were kept **outside the repo** at `~/hackrf-flash-backups/` on the
old workstation and are **not on this laptop**. Re-fetch from the upstream v2.4.0
release before you need them:

```bash
gh release download v2.4.0 -R portapack-mayhem/mayhem-firmware -p 'FIRMWARE_mayhem_v2.4.0.zip'
# want firmware/firmware_hackrf.bin (1MB, the `hackrf` variant) and hackrf_usb.dfu
```

---

## 4. Brick guard — run this before every flash

One line of source can brick the device: a namespace-scope object with a
non-trivial constructor emits a global initialiser inside the app's fake address
region, which the firmware calls at boot into unmapped memory.

```bash
readelf -sW build/firmware/application/application.elf \
  | grep -E '_GLOBAL__sub_I.*(rf_notebook|tl_fox24|tl_fox5|tl_foxham|tl_btmon|tl_logo)'
# must print NOTHING

readelf -sW build/firmware/application/application.elf | grep -cE '_ZGV.*tl_'
# guard variables from function-local statics -- must be 0
```

**`.init_array` does not exist in this ELF**, so checking for it comes back empty
and falsely looks like a clean bill of health. Check `_GLOBAL__sub_I` symbols.

Rules for app code: no namespace-scope non-trivial constructors (use
`constexpr std::u16string_view` and build paths locally), no function-local
statics, all storage as members.

Turning this into a build-time failure would be a genuine contribution upstream.

---

## 5. Current state

**Working, validated on hardware:**
- Firmware boots reliably; `ThemeTensorLab`, branded splash, TL Logo app
- RF Notebook **Milestone 2** — 100-mark run clean (100/100 CRC valid, contiguous
  sequence, no corruption), and Wi-Fi vs noise discrimination proven
  (`snr_spec` 50–62 vs 0; averaged-spectrum spread 82 vs 32)
- `tools/rfnotebook/` importer — validate / render / export / report

**Broken:** RF Notebook **Milestone 3** (auto-detection) **hangs the app on
launch**. Firmware is fine; a power cycle recovers.

**Untested:** all four new modules (`tl_fox24`, `tl_fox5`, `tl_foxham`,
`tl_btmon`). They compile and pass the brick guard, nothing more.

**Never built:** IQ capture, SigMF export, range scanning in RF Notebook,
fox-hunt bearing/antenna fields, gain logging in events.

---

## 6. Suggested first tasks, in order

### (a) Freqman rework of `tl_foxcore` — no hardware needed, start here

`tl_foxcore` hardcodes three compile-time band plans and its `Channel` struct
holds a single centre frequency, so it **cannot express a range at all**. Mayhem
already has the right mechanism and the modules ignore it: `freqman` reads
user-editable `/FREQMAN/*.TXT` with `freqman_type::Range` (`a=`/`b=`), and
`ui_recon`, `scanner`, `signal_hunter` and `level` already consume it.

Rework the plans to load from freqman, then write starter files (`TL_MIC.TXT`
etc.). Roadmap §1 has a wireless-mic range table — **flagged as unverified
against current FCC rules**, so confirm before trusting the 600 MHz edges.

Pure source work, ~13.4 KB headroom per app, and it makes the modules actually
useful. Do this while the flashing situation is idle.

### (b) Resolve the Milestone 3 launch hang

This blocks trusting **any** of the new modules, because all four enable the
receiver in their constructors — the same code path.

What is already known, so you don't repeat it:
- Hang occurs **before `start_session()`** (no session directory is created), so
  during construction, before any detection logic runs (`auto_armed_` starts
  false, so `evaluate_auto()` returns immediately).
- **Eliminated with evidence:** app size (22,680 bytes with 10 KB headroom still
  hung), SRAM overflow, baseband size, the `Checkbox` widget type, widget layout
  overlap, global constructors, guard variables, section placement.
- **Remaining suspects:** the two new widgets (`text_auto`, `button_auto`) —
  adding a focusable widget changes Mayhem's geometric focus-navigation graph —
  or the `rfsk::Detector` member.
- **Next test:** the validated `56c4f84` app plus *only* the two widgets, no
  detector. Hangs → widgets/focus. Launches → the `Detector`.

```bash
git checkout 56c4f84 -- firmware/application/external/rf_notebook/   # last good app
git checkout 4ffec4b -- firmware/application/external/rf_notebook/   # milestone 3
```

### (c) MDK bring-up

The operator now has the physical MDK: an **ESP32-S3 board on I2C**. Roadmap §2.
Two things to internalise first:

- MDK apps are **standalone `.ppmp` apps** (`firmware/standalone/`), *not*
  external `.ppma` apps. Different framework, descriptor and API version. Its
  reduced UI copy declares fonts as **functions** (`font::fixed_8x16()`).
- Use **ESP-IDF 5.3, not 5.4** — 5.4 broke the I2C subsystem.

Revised Milestone 0: build the stock UART example → prove PortaPack↔ESP32 I2C →
prove ESP32↔phone WiFi → *then* design the protocol. Stop at each step.

### (d) Companion app

Viable now the MDK exists. **One architectural problem to solve first: only one
app runs on the PortaPack at a time**, and RF Notebook is a `.ppma` while an MDK
link app is a `.ppmp` — so live streaming from the notebook is not achievable as
two separate apps. Roadmap §3 gives three options; store-and-forward is
recommended first.

---

## 7. Working with the operator

They are hands-on and tolerant of setbacks — they explicitly said the prestige of
custom firmware outweighs the bricking. But their time is the scarce resource:
every flash costs a power cycle, every brick costs a DFU cycle, and each `.ppma`
iteration through `sd_over_usb` costs two power cycles.

So: batch your changes into meaningful tests, tell them exactly which physical
action you need and why, and report results plainly — including when something
did not work. Do not claim a fix is validated when it has only compiled.
