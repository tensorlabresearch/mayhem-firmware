# Tensor Lab Hunt / Monitor Modules — Design

Four external apps. Scaffolding stage: **they compile and register; none has been
run on hardware.**

Read `TENSORLAB_HACKRF_NOTES.md` first — every constraint below comes from a trap
already paid for there.

| App | Menu name | Baseband | `.ppma` | Region |
|---|---|---|---|---|
| `tl_fox24` | WiFi 2.4 Fox | PWFM | 19,320 | `0xAE0E0000` |
| `tl_fox5` | WiFi 5G Fox | PWFM | 19,316 | `0xAE0F0000` |
| `tl_foxham` | Ham Fox Hunt | PWFM | 19,320 | `0xAE100000` |
| `tl_btmon` | BLE Monitor | PBTR | 23,232 | `0xAE110000` |

All four verified free of `_GLOBAL__sub_I` and `__cxa_guard` symbols — the two
patterns that brick the device at boot.

---

## Feasibility, stated plainly

### What the hardware genuinely supports

**RSSI-based direction finding** on any band the HackRF tunes (1 MHz – 6 GHz).
This is what fox hunting actually is: the operator sweeps a directional antenna
and walks toward the peak. The radio supplies signal strength; the human supplies
the direction finding.

**BLE advertising decode.** `proc_btlerx` emits `BLEPacketMessage` with a real
`BlePacketData { max_dB, type, macAddress[6], data[40], dataLen }`. Actual
packets, actual MAC addresses.

### What it does not

**No 802.11 anything.** There is no OFDM demodulator in this firmware. The WiFi
hunt modules can tell you *something is transmitting on channel 6*. They cannot
tell you the SSID, the BSSID, the MAC, or whether it is even WiFi rather than a
microwave oven or a video sender.

**No WiFi deauth detection — this request cannot be met.** A deauth is an 802.11
management frame. Detecting it requires demodulating OFDM and parsing the MAC
layer. Spectrum energy cannot distinguish a deauth flood from a file transfer, a
video stream, or a crowded channel. Any module claiming to detect deauths from
spectrum alone would be fabricating a result.

The closest honest equivalent is `tl_btmon`: **BLE advertising floods are a real
attack class, and they are genuinely detectable here** because the packets
decode. That is what was built instead.

**5 GHz is antenna-limited.** The band is in range, but a 2.4 GHz whip reads
5 GHz as quiet even when it is busy. A weak survey there means *check the
antenna* before it means *no signal*.

**`tl_foxham` overlaps `foxhunt_rx`.** Mayhem already ships a fox hunt app with a
GeoMap and marker logging. `tl_foxham` exists for band-plan sweeping and a
consistent gain workflow across the Tensor Lab modules; for a classic ARDF hunt
with mapping, `foxhunt_rx` is still the better tool. This is a deliberate
duplication, not an oversight.

---

## Architecture

```
tl_foxcore/foxcore.hpp   RssiTracker, SweepEngine, band plans, gain presets
tl_foxcore/foxview.hpp   FoxHuntView -- the whole hunt UI and behaviour
tl_fox24/  tl_fox5/  tl_foxham/     thin derivations: plan + title only
tl_btmon/                           separate; BLE packet path, not spectrum
```

**Header-only by necessity.** Each external app links as its own standalone
binary, collected by an object-name pattern in `external.ld`. A shared `.cpp`
cannot be pulled into two different app sections, so the core is inline and each
app compiles its own copy. Keep it small — it is duplicated four times.

### Two modes, deliberately exclusive

- **SURVEY** — sweep the band plan, remember peak RSSI per channel
- **HUNT** — park on one channel, show instantaneous + decaying peak hold

They are mutually exclusive on purpose. Sweeping while hunting would move the
reading for two reasons at once — retune and antenna heading — and the operator
could not tell which. Workflow is: survey to find the channel, `Next/Best` to
park on the strongest, then hunt by heading.

### Design decisions worth keeping

**Instantaneous + peak hold, never a cumulative average.** RF Notebook's
cumulative min/avg/max made it useless for hunting; that mistake is recorded in
the notes and not repeated. The peak decays (`peak_hold_ticks`) so a stale
maximum from a previous heading cannot walk the operator the wrong way.

**Survey reports its own confidence.** `SweepEngine::margin()` is best-minus-
median across channels. Below ~6 the UI says *"no clear peak"* rather than
pointing confidently at noise. A survey that found nothing should say so.

**Gain presets FAR / NEAR / TOP.** Closing on a fox saturates the front end, and
a saturated receiver reads flat exactly when the signal should be rising — the
classic beginner trap. Being able to wind gain *down* fast matters more than
absolute sensitivity, so it is one button.

**BLE census is fixed-size (48 MACs) and counts its own overflow.** A bounded
table cannot be exhausted by a flood, which is the condition being detected;
overflow is itself evidence and is surfaced, not silently dropped.

**BLE baseline learning stops while alerting.** Folding a live flood into
"normal" would blind the detector to exactly what it exists to catch — the same
trap as the RF Notebook detector.

**The verdict never claims an attack.** It reads *"unusual advertising rate"*.
An elevated rate has innocent explanations: a crowd, a beacon array, a phone in
pairing mode. The project plan forbids presenting a heuristic as an
identification.

---

## Status and next steps

**Done:** design, shared core, four apps, all four registration points, clean
build, brick-guard symbols verified absent.

**Not done — nothing has run on hardware.** In particular:

1. **Untested launch path.** Every module calls `baseband::run_prepared_image()`
   and enables the receiver in its constructor. That exact area is where the
   RF Notebook Milestone 3 hang lives and is still unresolved — see the notes.
   Expect to debug launch before anything else works.
2. **`tl_btmon` never configures the BLE baseband.** It starts PBTR and listens
   for `BlePacketMessage`, but `proc_btlerx` likely needs a
   `BTLERxConfigureMessage` before it emits anything. Check how the built-in BLE
   RX app configures it.
3. **Thresholds are guesses.** `flood_packets = 240`, `flood_unique = 30`, and
   the survey margin of 6 are starting points, not measurements. They must be
   calibrated against a real environment — a conference floor has a far higher
   BLE baseline than a lab, and shipping uncalibrated thresholds would generate
   confident nonsense.
4. **No RF Notebook integration.** These modules do not log to `/RFNOTE/`. Wiring
   a survey result or a BLE alert into an `events.jsonl` entry is the obvious
   next feature and would reuse the validated `.rfsk` path.
5. **Deploying requires the whole `/APPS` set.** Adding four apps shifts every
   other app's baked link addresses. Redeploy everything together with the
   firmware, never one app alone.

Firmware headroom after these four: **17,556 bytes** (was 22,588). Each app costs
roughly 1.2 KB of main-image glue.
