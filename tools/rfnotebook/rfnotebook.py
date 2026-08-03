#!/usr/bin/env python3
"""rfnotebook -- desktop importer for RF Field Notebook sessions.

Reads a PortaPack /RFNOTE/<session-id>/ directory and:
  validate  check CRC32s, header sanity, sequence continuity, missing artifacts
  render    waterfall PNGs from .rfsk spectral sketches
  export    events.csv and events.json (and events.geojson when location exists)
  report    a single self-contained HTML session report
  all       everything above

The .rfsk reader here is deliberately version-locked to
firmware/application/external/rf_notebook/rfsk.hpp in the same repository. The
header is pinned by static_assert on the device side and by HEADER_SIZE here; if
they ever diverge, validate fails loudly rather than silently misreading.

CRC32 is CRC-32/BZIP2 (poly 0x04C11DB7, init/xorout 0xFFFFFFFF, MSB-first) to
match Mayhem's CRC<32> and its `crc32` console command -- NOT zlib's reflected
variant. Getting this wrong produces a mismatch on perfectly good files.
"""

from __future__ import annotations

import argparse
import base64
import csv
import io
import json
import struct
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path

MAGIC = b"RFSK"
HEADER_SIZE = 48
BINS = 64
FRAMES = 16
SKETCH_SIZE = HEADER_SIZE + BINS + FRAMES * BINS + 4  # 1140


def crc32_bzip2(data: bytes) -> int:
    """MSB-first CRC-32 with poly 0x04C11DB7, matching Mayhem's CRC<32>."""
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b << 24
        for _ in range(8):
            crc = ((crc << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if crc & 0x80000000 else (crc << 1) & 0xFFFFFFFF
    return crc ^ 0xFFFFFFFF


@dataclass
class Sketch:
    path: Path
    version: int
    bins: int
    frames: int
    flags: int
    center_frequency_hz: int
    sample_rate_sps: int
    span_hz: int
    device_monotonic_ms: int
    event_seq: int
    rssi: int
    noise_floor: int
    snr: int
    peak_bin: int
    occupied_bandwidth_hz: int
    average: list[int] = field(default_factory=list)
    matrix: list[int] = field(default_factory=list)
    crc_ok: bool = False
    problems: list[str] = field(default_factory=list)

    @property
    def occupancy_ratio(self) -> float:
        """Fraction of matrix cells that are non-zero. Zero means the sketch
        carries no spectrum at all -- a failure mode that still CRCs cleanly."""
        return sum(1 for v in self.matrix if v) / max(1, len(self.matrix))

    @property
    def spectral_spread(self) -> int:
        """Range of the averaged spectrum. Noise sits low; real signal spreads."""
        return (max(self.average) - min(self.average)) if self.average else 0


def read_sketch(p: Path) -> Sketch:
    raw = p.read_bytes()
    problems: list[str] = []

    if len(raw) != SKETCH_SIZE:
        problems.append(f"size {len(raw)} != expected {SKETCH_SIZE}")
    if raw[:4] != MAGIC:
        problems.append(f"bad magic {raw[:4]!r}")

    ver, nb, nf, flags = struct.unpack_from("<HHHH", raw, 4)
    (cf,) = struct.unpack_from("<Q", raw, 12)
    sr, span, mono, seq = struct.unpack_from("<IIII", raw, 20)
    rssi, nfloor, snr, peak = struct.unpack_from("<hhhH", raw, 36)
    (obw,) = struct.unpack_from("<I", raw, 44)

    if (nb, nf) != (BINS, FRAMES):
        problems.append(f"dimensions {nb}x{nf} != {BINS}x{FRAMES}")

    avg = list(raw[HEADER_SIZE:HEADER_SIZE + BINS])
    mat = list(raw[HEADER_SIZE + BINS:HEADER_SIZE + BINS + FRAMES * BINS])
    (stored,) = struct.unpack_from("<I", raw, len(raw) - 4)
    calc = crc32_bzip2(raw[:-4])
    if stored != calc:
        problems.append(f"CRC32 stored 0x{stored:08X} != computed 0x{calc:08X}")

    s = Sketch(p, ver, nb, nf, flags, cf, sr, span, mono, seq,
               rssi, nfloor, snr, peak, obw, avg, mat, stored == calc, problems)

    # A sketch can pass CRC while containing nothing. Say so.
    if s.occupancy_ratio == 0.0:
        s.problems.append("matrix is entirely zero -- no spectrum captured")
    return s


@dataclass
class Session:
    directory: Path
    session_id: str
    device_json: dict
    events: list[dict]
    sketches: dict[int, Sketch]
    problems: list[str]


def load_session(d: Path) -> Session:
    problems: list[str] = []

    dev_p = d / "session-device.json"
    device_json: dict = {}
    if dev_p.exists():
        try:
            device_json = json.loads(dev_p.read_text())
        except json.JSONDecodeError as e:
            problems.append(f"session-device.json is not valid JSON: {e}")
    else:
        problems.append("session-device.json missing")

    events: list[dict] = []
    ev_p = d / "events.jsonl"
    if ev_p.exists():
        for i, line in enumerate(ev_p.read_text().splitlines(), 1):
            line = line.strip()
            if not line:
                continue
            try:
                events.append(json.loads(line))
            except json.JSONDecodeError as e:
                problems.append(f"events.jsonl line {i} is not valid JSON: {e}")
    else:
        problems.append("events.jsonl missing")

    sketches: dict[int, Sketch] = {}
    sk_dir = d / "sketches"
    if sk_dir.is_dir():
        for f in sorted(sk_dir.glob("*.rfsk")):
            s = read_sketch(f)
            sketches[s.event_seq] = s

    return Session(d, device_json.get("session_id", d.name), device_json,
                   events, sketches, problems)


def cmd_validate(sess: Session) -> int:
    print(f"session {sess.session_id}   {sess.directory}")
    print(f"  events: {len(sess.events)}   sketches: {len(sess.sketches)}")

    issues: list[str] = list(sess.problems)

    # sequence continuity
    seqs = sorted(e.get("event_seq", -1) for e in sess.events)
    if seqs:
        expected = list(range(1, len(seqs) + 1))
        if seqs != expected:
            missing = sorted(set(expected) - set(seqs))
            dupes = sorted({x for x in seqs if seqs.count(x) > 1})
            if missing:
                issues.append(f"event_seq gaps: missing {missing[:20]}"
                              + (" ..." if len(missing) > 20 else ""))
            if dupes:
                issues.append(f"duplicate event_seq: {dupes[:20]}")

    # artifact cross-reference, both directions
    for e in sess.events:
        seq = e.get("event_seq")
        ref = e.get("spectral_sketch")
        if ref:
            if not (sess.directory / ref).exists():
                issues.append(f"event {seq} references missing artifact {ref}")
        if seq not in sess.sketches:
            issues.append(f"event {seq} has no sketch file")
    for seq in sess.sketches:
        if seq not in seqs:
            issues.append(f"orphan sketch E{seq:06d}.rfsk with no event record")

    bad_crc = [s for s in sess.sketches.values() if not s.crc_ok]
    empty = [s for s in sess.sketches.values() if s.occupancy_ratio == 0.0]
    print(f"  CRC32 valid: {len(sess.sketches) - len(bad_crc)}/{len(sess.sketches)}")
    print(f"  non-empty matrices: {len(sess.sketches) - len(empty)}/{len(sess.sketches)}")

    for s in sess.sketches.values():
        for p in s.problems:
            issues.append(f"{s.path.name}: {p}")

    if issues:
        print(f"\n  {len(issues)} PROBLEM(S):")
        for i in issues[:60]:
            print(f"    - {i}")
        if len(issues) > 60:
            print(f"    ... and {len(issues) - 60} more")
        return 1
    print("\n  OK -- no problems found")
    return 0


def matrix_png(s: Sketch, scale_x: int = 6, scale_y: int = 12,
               lo: int | None = None, hi: int | None = None) -> bytes:
    """Render the sketch matrix as a greyscale PNG. Frequency across, time down."""
    try:
        from PIL import Image
    except ImportError:
        raise SystemExit("render/report need Pillow: pip install pillow")
    lo = min(s.matrix) if lo is None else lo
    hi = max(s.matrix) if hi is None else hi
    rng = max(1, hi - lo)
    img = Image.new("L", (s.bins, s.frames))
    img.putdata([int(255 * (v - lo) / rng) for v in s.matrix])
    img = img.resize((s.bins * scale_x, s.frames * scale_y), Image.NEAREST)
    buf = io.BytesIO()
    img.save(buf, format="PNG")
    return buf.getvalue()


def cmd_render(sess: Session, outdir: Path) -> int:
    outdir.mkdir(parents=True, exist_ok=True)
    # Common scale across the session so events are visually comparable.
    all_vals = [v for s in sess.sketches.values() for v in s.matrix]
    lo, hi = (min(all_vals), max(all_vals)) if all_vals else (0, 255)
    for seq, s in sorted(sess.sketches.items()):
        p = outdir / f"E{seq:06d}.png"
        p.write_bytes(matrix_png(s, lo=lo, hi=hi))
    print(f"rendered {len(sess.sketches)} waterfalls to {outdir}  (shared scale {lo}..{hi})")
    return 0


def event_rows(sess: Session) -> list[dict]:
    rows = []
    for e in sess.events:
        seq = e.get("event_seq")
        s = sess.sketches.get(seq)
        row = dict(e)
        row.pop("labels", None)
        row["labels"] = "|".join(e.get("labels", []))
        if s:
            row["sketch_occupancy_ratio"] = round(s.occupancy_ratio, 3)
            row["sketch_spectral_spread"] = s.spectral_spread
            row["sketch_crc_ok"] = s.crc_ok
            row["sketch_peak_bin"] = s.peak_bin
        rows.append(row)
    return rows


def cmd_export(sess: Session, outdir: Path) -> int:
    outdir.mkdir(parents=True, exist_ok=True)
    rows = event_rows(sess)

    (outdir / "events.json").write_text(json.dumps(
        {"session": sess.device_json, "events": rows}, indent=2))

    if rows:
        cols: list[str] = []
        for r in rows:
            for k in r:
                if k not in cols:
                    cols.append(k)
        with (outdir / "events.csv").open("w", newline="") as f:
            w = csv.DictWriter(f, fieldnames=cols)
            w.writeheader()
            w.writerows(rows)

    # GeoJSON only when a companion device actually supplied coordinates.
    feats = []
    for r in rows:
        loc = r.get("phone_location") or {}
        lat, lon = loc.get("latitude"), loc.get("longitude")
        if lat is not None and lon is not None:
            feats.append({"type": "Feature",
                          "geometry": {"type": "Point", "coordinates": [lon, lat]},
                          "properties": r})
    if feats:
        (outdir / "events.geojson").write_text(json.dumps(
            {"type": "FeatureCollection", "features": feats}, indent=2))
        print(f"exported events.csv, events.json, events.geojson ({len(feats)} located) to {outdir}")
    else:
        print(f"exported events.csv, events.json to {outdir}")
        print("  no GeoJSON: no event carries phone_location "
              "(expected -- the device cannot know location)")
    return 0


def triage_rank(sess: Session) -> list[tuple[int, float, str]]:
    """Rank events by how much they look like signal rather than noise.

    Transparent and additive on purpose: plan section 6 asks for an explainable
    score, not a classifier. snr_spec and spectral spread are the two measures
    validated against a real signal, so they carry the weight.
    """
    out = []
    for e in sess.events:
        seq = e.get("event_seq")
        s = sess.sketches.get(seq)
        snr = e.get("snr_spec") or 0
        spread = s.spectral_spread if s else 0
        obw = e.get("occupied_bandwidth_hz") or 0
        score = 0.0
        why = []
        if snr:
            score += min(snr / 60.0, 1.0) * 0.5
            why.append(f"snr={snr}")
        if spread:
            score += min(spread / 80.0, 1.0) * 0.35
            why.append(f"spread={spread}")
        if obw:
            score += 0.15
            why.append(f"obw={obw}Hz")
        if s and s.occupancy_ratio == 0.0:
            score = 0.0
            why = ["empty sketch"]
        out.append((seq, round(score, 3), ", ".join(why) or "no evidence"))
    return sorted(out, key=lambda t: -t[1])


def cmd_report(sess: Session, outdir: Path) -> int:
    outdir.mkdir(parents=True, exist_ok=True)
    all_vals = [v for s in sess.sketches.values() for v in s.matrix]
    lo, hi = (min(all_vals), max(all_vals)) if all_vals else (0, 255)
    ranked = triage_rank(sess)

    css = """body{font:14px/1.5 system-ui,sans-serif;background:#050203;color:#fff5ef;margin:0;padding:24px}
h1,h2{font-weight:600;color:#fff5ef}h1{border-bottom:2px solid #b61225;padding-bottom:8px}
table{border-collapse:collapse;width:100%;margin:12px 0}
th,td{text-align:left;padding:6px 10px;border-bottom:1px solid #3d050d;font-variant-numeric:tabular-nums}
th{color:#c28d87;font-weight:600}code{color:#ef233c}
.ev{display:flex;gap:16px;align-items:flex-start;padding:14px 0;border-bottom:1px solid #3d050d}
.ev img{image-rendering:pixelated;border:1px solid #3d050d}
.meta{font-size:13px}.warn{color:#ff3d54}.ok{color:#40d070}"""

    h = [f"<!doctype html><meta charset=utf-8><title>RF Notebook {sess.session_id}</title>",
         f"<style>{css}</style>", f"<h1>RF Field Notebook — {sess.session_id}</h1>"]

    h.append("<h2>Session</h2><table>")
    for k, v in (sess.device_json or {}).items():
        h.append(f"<tr><th>{k}</th><td>{v}</td></tr>")
    h.append(f"<tr><th>events</th><td>{len(sess.events)}</td></tr>")
    h.append(f"<tr><th>sketches</th><td>{len(sess.sketches)}</td></tr>")
    bad = [s for s in sess.sketches.values() if not s.crc_ok]
    cls = "warn" if bad else "ok"
    h.append(f"<tr><th>CRC32</th><td class={cls}>"
             f"{len(sess.sketches)-len(bad)}/{len(sess.sketches)} valid</td></tr></table>")

    h.append("<h2>Triage ranking</h2>"
             "<p class=meta>Additive, explainable score. Heuristic only — it indicates "
             "which events are worth a closer look, not what they are.</p>"
             "<table><tr><th>rank</th><th>event</th><th>score</th><th>evidence</th></tr>")
    for i, (seq, score, why) in enumerate(ranked, 1):
        h.append(f"<tr><td>{i}</td><td>E{seq:06d}</td><td>{score}</td><td>{why}</td></tr>")
    h.append("</table>")

    h.append("<h2>Events</h2>")
    for e in sess.events:
        seq = e.get("event_seq")
        s = sess.sketches.get(seq)
        h.append("<div class=ev>")
        if s:
            b64 = base64.b64encode(matrix_png(s, lo=lo, hi=hi)).decode()
            h.append(f"<img src='data:image/png;base64,{b64}' "
                     f"alt='sketch E{seq:06d}' title='frequency across, time down'>")
        h.append("<div class=meta>")
        h.append(f"<b>E{seq:06d}</b> &nbsp; {e.get('observed_at_device_local','?')}<br>")
        h.append(f"{int(e.get('center_frequency_hz',0))/1e6:.4f} MHz &nbsp; "
                 f"sr={e.get('sample_rate_sps')} <br>")
        h.append(f"snr_spec={e.get('snr_spec')} &nbsp; nf={e.get('noise_floor_spec')} &nbsp; "
                 f"spread={e.get('noise_spread_spec')} &nbsp; obw={e.get('occupied_bandwidth_hz')} Hz<br>")
        if s:
            h.append(f"matrix non-zero {s.occupancy_ratio*100:.0f}% &nbsp; "
                     f"spectral spread {s.spectral_spread} &nbsp; "
                     + ("<span class=ok>CRC ok</span>" if s.crc_ok else "<span class=warn>CRC BAD</span>")
                     + "<br>")
        note = e.get("note") or ""
        if note:
            h.append(f"note: <i>{note}</i><br>")
        h.append("</div></div>")

    out = outdir / "report.html"
    out.write_text("".join(h))
    print(f"wrote {out}")
    return 0


def find_sessions(p: Path) -> list[Path]:
    if (p / "events.jsonl").exists() or (p / "session-device.json").exists():
        return [p]
    return sorted(d for d in p.iterdir() if d.is_dir() and
                  ((d / "events.jsonl").exists() or (d / "session-device.json").exists()))


def main() -> int:
    ap = argparse.ArgumentParser(prog="rfnotebook",
                                 description="Importer for RF Field Notebook sessions")
    ap.add_argument("command", choices=["validate", "render", "export", "report", "all"])
    ap.add_argument("path", type=Path,
                    help="a session directory, or a /RFNOTE root containing several")
    ap.add_argument("-o", "--out", type=Path, default=Path("rfnotebook-out"))
    a = ap.parse_args()

    if not a.path.exists():
        print(f"no such path: {a.path}", file=sys.stderr)
        return 2

    sessions = find_sessions(a.path)
    if not sessions:
        print(f"no sessions found under {a.path}", file=sys.stderr)
        return 2

    rc = 0
    for d in sessions:
        sess = load_session(d)
        out = a.out / sess.session_id
        if a.command in ("validate", "all"):
            rc |= cmd_validate(sess)
        if a.command in ("render", "all"):
            rc |= cmd_render(sess, out / "waterfalls")
        if a.command in ("export", "all"):
            rc |= cmd_export(sess, out)
        if a.command in ("report", "all"):
            rc |= cmd_report(sess, out)
        print()
    return rc


if __name__ == "__main__":
    sys.exit(main())
