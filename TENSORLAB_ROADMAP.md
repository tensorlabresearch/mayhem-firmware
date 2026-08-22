# Tensor Lab PortaPack — Roadmap and Open Design

Companion to `TENSORLAB_HACKRF_NOTES.md` (platform traps) and
`TENSORLAB_MODULES.md` (the four hunt/monitor modules).

This file is forward-looking: the frequency-preset gap, the MDK module work, and
the Android companion app that the MDK makes viable.

---

## 1. Frequency presets — a real gap in the modules I shipped

### The problem

`tl_foxcore` has **three compile-time band plans** (`plan_wifi24`, `plan_wifi5`,
`plan_ham`) and **zero runtime loading**. Consequences:

- No wireless-mic ranges, no ISM sweeps, no contest fox frequencies
- Adding a range means editing C++ and reflashing firmware
- The `Channel` struct holds a single centre frequency, so it **cannot express a
  range at all**. "Sweep 470–608 MHz" is inexpressible without enumerating
  hundreds of centres by hand

That was a design mistake. Mayhem already has the right mechanism and the modules
don't use it.

### What already exists

`freqman` — user-editable `.TXT` files in `/FREQMAN/` on the SD card:

- `freqman_type::Single` (`f=`) and **`freqman_type::Range` (`a=`, `b=`)**
- `load_freqman_file(stem, db, options)`, `get_freqman_path(stem)`,
  `to_freqman_string(entry)`, `parse_freqman_entry()`
- Already consumed by `ui_recon`, `scanner`, `signal_hunter` and `level`
- Community files at `portapack-mayhem/mayhem-freqman-files`

### What works today, with no new code

**Drop a freqman file into `/FREQMAN/` and use Recon or Scanner.** They sweep
ranges already. That is the honest answer to "can the existing app do this" — the
*Mayhem* apps can; the Tensor Lab modules cannot.

### The rework

Replace the hardcoded `Channel` tables with freqman-loaded plans:

- Presets become editable text on the card instead of a recompile
- Range entries become expressible, which is the whole point for mic hunting
- Same mechanism the rest of the firmware already uses, so files are shareable

Headroom is available: each fox app has ~13.4 KB spare of its 32 KB budget.

Starter files to write: `TL_MIC.TXT`, `TL_WIFI24.TXT`, `TL_WIFI5.TXT`,
`TL_HAM.TXT`.

### Wireless microphone ranges (verify before trusting)

| Range | Notes |
|---|---|
| 174–216 MHz | VHF, US TV ch 7–13 |
| 470–608 MHz | UHF, the main band |
| 614–616, 657–663 MHz | guard bands |
| 902–928 MHz | ISM |
| 944–952 MHz | US studio-transmitter link |
| 1920–1930 MHz | US DECT 6.0 |
| 2400–2483.5 MHz | 2.4 GHz digital |

**Check current regulations before relying on the 600 MHz edges.** The US 600 MHz
band was auctioned in 2017 and much of 617–652 / 663–698 MHz is no longer
available to wireless mics. This is region-specific and I have not verified it
against current FCC rules — treat the table as a starting point, not authority.

---

## 2. MDK module work

### What the MDK actually is

`portapack-mayhem/mayhem-mdk` — "External Module Developer Kit" for
HackRF+PortaPack H4. **An ESP32-S3 board on I2C**, with open PCB files (JLCPCB
production set in `pcb/production`).

Two separate software halves:

1. **A standalone app on the PortaPack**, which gets *embedded in the ESP32
   firmware image* and is uploaded from there. Guide: `src/uart/Readme.md`.
2. **ESP32-S3 firmware**, built with **ESP-IDF 5.3** — *not* 5.4, which
   introduced a breaking change in the I2C subsystem. Guide:
   `src/portapack-external-module/README.md`.

The stock example is a UART app that appears in the Utilities menu.

### Standalone apps are NOT external apps — this matters

Two different frameworks, and the MDK uses the one we have not touched:

| | External app | Standalone app |
|---|---|---|
| Source tree | `firmware/application/external/` | `firmware/standalone/` |
| Artifact | `.ppma` | `.ppmp` |
| Descriptor | `application_information_t` | `standalone_application_information_t` |
| API version | `CURRENT_HEADER_VERSION` = 3 | `CURRENT_STANDALONE_APPLICATION_API_VERSION` = 4 |
| Examples in tree | 58 of them | `digitalrain`, `pacman` |

`digitalrain` and `pacman` are exactly the two `.ppmp` files sitting on the SD
card, which confirms the mapping.

**Correction to an earlier claim.** Commit `6a05f6c` justified patching
`firmware/standalone/common/ui/theme.cpp` by saying it "is compiled into the
external `.ppma` apps". That is **wrong** — it serves standalone `.ppmp` apps.
The patch itself was still worth doing and is still correct, but for a different
reason than stated, and that reason is now load-bearing: **MDK apps are standalone
apps, so `ThemeTensorLab` in the standalone theme is what will brand them.**

### Unknowns to establish before building

- Standalone app size limit, and whether it shares the 32 KB external-app budget
- Which UI widgets standalone apps can use (`firmware/standalone/common/ui/` is a
  reduced copy — note its fonts are *functions*, `font::fixed_8x16()`, unlike the
  application copy)
- Whether the brick hazards from the notes apply identically. Assume they do:
  **no namespace-scope non-trivial constructors, no function-local statics.**
- Whether a standalone app can reach the SD card

---

## 3. Android companion app — now viable

### Why it was blocked, and why it isn't

The project plan (§3.2, §9, §10) assumed a phone connected over **USB Web
Serial**, forking MayhemHub. That was dropped for two good reasons:

1. Mayhem's only app-to-host push is `UsbSerialAsyncmsg`, whose own header warns
   it is *"not real async"* and that concurrent transmissions corrupt each other.
   Building a protocol on it would have been throwaway work.
2. A wifi module was planned, which would change the transport anyway.

**The MDK is that module.** The ESP32-S3 has WiFi and BLE on board, so the
transport becomes:

```
PortaPack app  <--I2C-->  ESP32-S3 (MDK)  <--WiFi/BLE-->  Android phone
```

This removes the `asyncmsg` blocker completely. The plan's §10 newline-delimited
JSON is still a fine wire format; it just runs over TCP or a WebSocket instead of
a serial console.

### Division of labour (unchanged from the plan, still right)

- **Phone supplies context**: GPS + accuracy, UTC anchor, venue and area labels,
  notes, session management, export
- **PortaPack supplies RF evidence**: tuning, gain, RSSI, noise floor, occupied
  bandwidth, `.rfsk` spectral sketches, `events.jsonl`

### The architectural problem to solve first

**Only one app runs on the PortaPack at a time.** RF Notebook is an external
`.ppma`; the MDK link app is a standalone `.ppmp`. They cannot both be running,
so "RF Notebook streams events to the phone live" is not achievable as two
separate apps.

Three options, in increasing order of work:

1. **Store-and-forward.** RF Notebook writes to SD as it does now. A separate
   sync step later ships `events.jsonl` and the sketches to the phone. Loses the
   plan's "<1 s from MARK to phone event" goal but needs no new device code and
   preserves the offline-first principle. **Probably the right first step.**
2. **Teach RF Notebook to talk I2C directly.** One app, live streaming, keeps the
   sub-second target. Costs code in an app already at 90% of its 32 KB budget,
   and couples the notebook to the module.
3. **ESP32 does more of the work.** The module holds session state and the phone
   talks only to it. Most flexible, most firmware to write, and the PortaPack
   still has to hand events over somehow.

Option 1 also degrades gracefully, which the plan explicitly asks for (§2.7,
"the PortaPack must continue logging when the phone cable is disconnected").

### Revised Milestone 0

The plan's original Milestone 0 was "prove Android Web Serial connectivity".
That is obsolete. The replacement, in order:

1. Build the stock MDK UART example and confirm the module is detected
2. Prove PortaPack ↔ ESP32 I2C round-trip with our own payload
3. Prove ESP32 ↔ phone over WiFi (ESP32 as AP is simplest in the field — no
   infrastructure, works in a hall with no usable network)
4. Only then design the event protocol

Stop and resolve each before moving on. The plan was right about that even if the
transport changed.

### Notes for the phone side

- **Offline first.** A conference hall has no usable network; the ESP32 as its own
  AP avoids depending on one.
- The plan wanted an installable PWA. With WiFi transport that still works and
  keeps one codebase — but a PWA cannot easily hold a socket in the background on
  Android, so a native app may be needed if live streaming matters. Store-and-
  forward sidesteps this too.
- Reuse the phone keyboard's voice dictation for notes rather than implementing
  speech recognition (plan §8).
- Poor GPS accuracy must stay **visible**, not hidden — indoors at a convention
  centre it will be bad, and a manual area label is the reliable field (§8).

---

## 4. Priority order

1. **Freqman rework of `tl_foxcore`** + starter preset files. Pure source, no
   hardware, fixes a real gap, and makes the modules useful for mic hunting.
2. **Resolve the RF Notebook Milestone 3 launch hang.** Still open, and all four
   new modules enable the receiver in their constructors — the same code path.
   Any of them may hit it. This blocks trusting *any* of the new modules.
3. **MDK bring-up**, steps 1–3 of the revised Milestone 0.
4. **Store-and-forward phone sync**, then decide whether live streaming is worth
   options 2 or 3 above.
5. Field docs the plan asks for and that still do not exist: `FIELD_TEST.md`,
   `KNOWN_LIMITATIONS.md`.

## 5. Still true, still unbuilt

IQ capture (plan Level 2) and SigMF export; scanning across a range in RF
Notebook (Milestone 4, blocked on the 32 KB ceiling); fox-hunt bearing and
antenna fields; gain/attenuation logging in event records.
