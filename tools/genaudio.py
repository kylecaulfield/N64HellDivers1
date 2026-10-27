#!/usr/bin/env python3
"""Generate HIVEBREAKERS' music and sound effects.

Usage: tools/genaudio.py OUT_DIR

Writes OUT_DIR/music/*.xm (FastTracker II modules) and OUT_DIR/sfx/*.wav.
The build converts them with libdragon's audioconv64 (to .xm64 / .wav64).

Everything is synthesized here from oscillators, noise and envelopes, and the
music is composed here as note data: original work, no samples or melodies
taken from anywhere. Standard library only, deterministic (same bytes every
run), so the build only reconverts when this script changes.

Music: D minor throughout.
  title    92 BPM  pads, bell arpeggios, a slow melody
  mission  132 BPM bass ostinato, drums, pads; the lead enters on the repeat
  hold     150 BPM the extraction hold: driving 16ths, brass stabs, siren lead
  victory  a short D major fanfare (plays once)
  defeat   a short falling phrase (plays once)
All modules use the same 8 channels: kick, snare, hats, bass, 3 pad voices,
lead.
"""

import math
import os
import struct
import sys

# ---------------------------------------------------------------------------
# Synthesis

class Rng:
    """Small LCG so the noise is the same on every run and machine."""

    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s

    def uniform(self):
        return (self.next() >> 8) / float(1 << 24) * 2.0 - 1.0


def lowpass(xs, k):
    """One-pole lowpass; k in (0, 1], smaller is darker."""
    out, y = [], 0.0
    for x in xs:
        y += k * (x - y)
        out.append(y)
    return out


def highpass(xs, k):
    lp = lowpass(xs, k)
    return [x - l for x, l in zip(xs, lp)]


def normalize(xs, peak=0.95):
    m = max(1e-9, max(abs(x) for x in xs))
    return [x * peak / m for x in xs]


def noise(n, seed):
    r = Rng(seed)
    return [r.uniform() for _ in range(n)]


def sweep_tone(rate, dur, f0, f1, curve=0.05, wave="sine", amp_decay=None, seed=1):
    """A tone whose pitch glides from f0 towards f1 (exponentially, time
    constant `curve` seconds), with an optional exponential amplitude decay."""
    n = int(rate * dur)
    out, ph = [], 0.0
    for i in range(n):
        t = i / rate
        f = f1 + (f0 - f1) * math.exp(-t / curve)
        ph += f / rate
        p = ph % 1.0
        if wave == "sine":
            s = math.sin(2 * math.pi * p)
        elif wave == "square":
            s = 1.0 if p < 0.5 else -1.0
        elif wave == "saw":
            s = 2.0 * p - 1.0
        else:  # triangle
            s = 4.0 * abs(p - 0.5) - 1.0
        if amp_decay:
            s *= math.exp(-t / amp_decay)
        out.append(s)
    return out


def chirp(rate, dur, f0, f1):
    """A sine gliding linearly from f0 to f1."""
    n, ph, out = int(rate * dur), 0.0, []
    for i in range(n):
        ph += (f0 + (f1 - f0) * i / n) / rate
        out.append(math.sin(2 * math.pi * ph))
    return out


def env_ad(n, rate, attack, decay):
    """Linear attack then exponential decay, as a gain per sample."""
    out = []
    for i in range(n):
        t = i / rate
        a = min(1.0, t / attack) if attack > 0 else 1.0
        out.append(a * math.exp(-max(0.0, t - attack) / decay))
    return out


def mix(*tracks):
    n = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(n)]


def scale(xs, g):
    return [x * g for x in xs]


def mul(xs, env):
    return [x * e for x, e in zip(xs, env)]


def cycle(n, harmonics):
    """One cycle of n samples from (harmonic, amplitude) pairs."""
    out = []
    for i in range(n):
        p = 2 * math.pi * i / n
        out.append(sum(a * math.sin(h * p) for h, a in harmonics))
    return normalize(out, 0.9)


def pulse_cycle(n, duty):
    return [0.8 if i < n * duty else -0.8 for i in range(n)]


def to_int16(xs):
    return [max(-32767, min(32767, int(round(x * 32767)))) for x in xs]


# ---------------------------------------------------------------------------
# WAV writer

def write_wav(path, rate, xs):
    data = struct.pack("<%dh" % len(xs), *to_int16(xs))
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate, rate * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


# ---------------------------------------------------------------------------
# XM writer (FastTracker II "Extended Module", version 0x0104)

NOTE_OFF = 97
SAMPLE_RATE_ONESHOT = 16726  # with relative note +12, C-4 plays it at its own rate


def note(name):
    """'C-4', 'C#4', 'Bb3' -> XM note number (1 = C-0)."""
    letter = name[0].upper()
    base = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}[letter]
    rest = name[1:]
    if rest[0] == "#":
        base += 1
        rest = rest[1:]
    elif rest[0] == "b":
        base -= 1
        rest = rest[1:]
    elif rest[0] == "-":
        rest = rest[1:]
    return 1 + int(rest) * 12 + base


def transpose(n, semis):
    return n + semis


class Instrument:
    def __init__(self, name, data, loop_start=0, loop_len=0, volume=64, relnote=0, finetune=0,
                 env=None, sustain=None, fadeout=0):
        self.name = name
        self.data = to_int16(data)
        self.loop_start, self.loop_len = loop_start, loop_len
        self.volume, self.relnote, self.finetune = volume, relnote, finetune
        self.env = env or []          # [(tick, 0..64)]
        self.sustain = sustain        # index of the sustain point
        self.fadeout = fadeout        # after key-off, per tick (0..4095)

    def header(self):
        h = struct.pack("<I", 263) + self.name.encode()[:22].ljust(22, b"\0") + bytes([0]) + struct.pack("<H", 1)
        h += struct.pack("<I", 40) + bytes(96)                       # sample header size, keymap
        pts = list(self.env[:12])
        h += b"".join(struct.pack("<HH", x, y) for x, y in pts) + bytes(4 * (12 - len(pts)))
        h += bytes(48)                                              # panning envelope
        env_type = 0
        if pts:
            env_type |= 1
            if self.sustain is not None:
                env_type |= 2
        h += bytes([len(pts), 0, self.sustain or 0, 0, 0, 0, 0, 0, env_type, 0])
        h += bytes([0, 0, 0, 0])                                    # vibrato
        h += struct.pack("<H", self.fadeout) + bytes(2)
        h = h.ljust(263, b"\0")
        loop_type = 1 if self.loop_len else 0
        sh = struct.pack("<III", len(self.data) * 2, self.loop_start * 2, self.loop_len * 2)
        sh += struct.pack("<BbBBbB", self.volume, self.finetune, loop_type | 0x10, 128, self.relnote, 0)
        sh += self.name.encode()[:22].ljust(22, b"\0")
        # Sample data: 16-bit deltas.
        prev, deltas = 0, []
        for s in self.data:
            d = (s - prev + 32768) % 65536 - 32768
            deltas.append(d)
            prev = s
        return h + sh + struct.pack("<%dh" % len(deltas), *deltas)


class Song:
    CHANNELS = 8

    def __init__(self, name, bpm, speed=6):
        self.name, self.bpm, self.speed = name, bpm, speed
        self.patterns = []   # each: list of rows; row: list of CHANNELS cells (n, i, v, fx, p)
        self.order = []
        self.restart = 0

    def new_pattern(self, rows=64):
        self.patterns.append([[None] * self.CHANNELS for _ in range(rows)])
        return len(self.patterns) - 1

    def put(self, pat, row, ch, n=0, ins=0, vol=None, fx=0, param=0):
        self.patterns[pat][row][ch] = (n, ins, (0x10 + vol) if vol is not None else 0, fx, param)

    def write(self, path, instruments):
        out = b"Extended Module: " + self.name.encode()[:20].ljust(20, b" ") + b"\x1a"
        out += b"hivebreakers gen".ljust(20, b" ") + struct.pack("<H", 0x0104)
        order = bytes(self.order).ljust(256, b"\0")
        out += struct.pack("<IHHHHHHHH", 276, len(self.order), self.restart, self.CHANNELS,
                           len(self.patterns), len(instruments), 1, self.speed, self.bpm) + order
        for rows in self.patterns:
            data = b""
            for row in rows:
                for cell in row:
                    if cell is None:
                        data += b"\x80"
                        continue
                    n, ins, vol, fx, p = cell
                    flags = 0x80 | (1 if n else 0) | (2 if ins else 0) | (4 if vol else 0) | \
                        (8 if fx else 0) | (16 if p else 0)
                    data += bytes([flags])
                    for present, v in ((n, n), (ins, ins), (vol, vol), (fx, fx), (p, p)):
                        if present:
                            data += bytes([v])
            out += struct.pack("<IBHH", 9, 0, len(rows), len(data)) + data
        for ins in instruments:
            out += ins.header()
        with open(path, "wb") as f:
            f.write(out)


# ---------------------------------------------------------------------------
# Instruments (shared by every song; index = position + 1)

def make_instruments():
    R = SAMPLE_RATE_ONESHOT
    kick = mix(sweep_tone(R, 0.28, 150, 42, curve=0.035, amp_decay=0.16),
               mul(noise(int(R * 0.01), 3), [1 - i / (R * 0.01) for i in range(int(R * 0.01))]))
    snare_body = sweep_tone(R, 0.2, 260, 180, curve=0.02, wave="triangle", amp_decay=0.05)
    snare_noise = mul(lowpass(noise(int(R * 0.24), 11), 0.6), env_ad(int(R * 0.24), R, 0.001, 0.07))
    snare = mix(scale(snare_body, 0.6), snare_noise)
    hat = mul(highpass(noise(int(R * 0.07), 23), 0.25), env_ad(int(R * 0.07), R, 0.0005, 0.018))
    crash = mul(highpass(noise(int(R * 1.1), 29), 0.18), env_ad(int(R * 1.1), R, 0.002, 0.35))

    # Tonal instruments loop one cycle: 64 samples with relnote +12, so C-4
    # plays at 16726/64 = 261.3 Hz (middle C is 261.6: 2 cents flat).
    bass = cycle(64, [(h, 1.0 / h ** 1.3) for h in range(1, 9)])          # mellow saw
    # Softened 25% pulse: filter three cycles and keep the last (settled) one.
    lead = lowpass(pulse_cycle(64, 0.25) * 3, 0.5)[128:]
    pad = cycle(64, [(1, 1.0), (2, 0.35), (3, 0.2), (4, 0.1), (6, 0.05)])  # organ-ish
    bell = cycle(64, [(1, 1.0), (3, 0.3), (5, 0.12)])
    brass = cycle(64, [(h, 1.0 / h) for h in range(1, 12)])              # bright saw

    return [
        Instrument("kick", normalize(kick), volume=64, relnote=12),
        Instrument("snare", normalize(snare), volume=52, relnote=12),
        Instrument("hat", normalize(hat), volume=30, relnote=12),
        Instrument("bass", bass, 0, 64, volume=50, relnote=12,
                   env=[(0, 64), (6, 48), (40, 40)], sustain=2, fadeout=2400),
        Instrument("lead", lead, 0, 64, volume=34, relnote=12,
                   env=[(0, 0), (2, 64), (10, 50), (60, 44)], sustain=3, fadeout=1200),
        Instrument("pad", pad, 0, 64, volume=24, relnote=12,
                   env=[(0, 0), (24, 64), (60, 56)], sustain=2, fadeout=500),
        Instrument("bell", bell, 0, 64, volume=36, relnote=12,
                   env=[(0, 64), (8, 36), (40, 14), (120, 0)], fadeout=800),
        Instrument("brass", brass, 0, 64, volume=30, relnote=12,
                   env=[(0, 64), (4, 40), (16, 20), (40, 0)], fadeout=2000),
        Instrument("crash", normalize(crash), volume=36, relnote=12),
    ]


KICK, SNARE, HAT, BASS, LEAD, PAD, BELL, BRASS, CRASH = range(1, 10)
CH_KICK, CH_SNARE, CH_HAT, CH_BASS, CH_PAD0, CH_PAD1, CH_PAD2, CH_LEAD = range(8)
C4 = note("C-4")

# Chords as (bass root, pad voicing), in D minor.
CHORDS = {
    "Dm": ("D-2", ["D-4", "F-4", "A-4"]),
    "Bb": ("Bb1", ["D-4", "F-4", "Bb4"]),
    "C":  ("C-2", ["E-4", "G-4", "C-5"]),
    "F":  ("F-2", ["C-4", "F-4", "A-4"]),
    "Gm": ("G-1", ["D-4", "G-4", "Bb4"]),
    "A":  ("A-1", ["C#4", "E-4", "A-4"]),
    "D":  ("D-2", ["D-4", "F#4", "A-4"]),
}


def put_drums(s, pat, bar, kicks, snares, hats, hat_vol=40, row0=0):
    base = row0 + bar * 16
    for r in kicks:
        s.put(pat, base + r, CH_KICK, C4, KICK)
    for r in snares:
        s.put(pat, base + r, CH_SNARE, C4, SNARE)
    for r in hats:
        s.put(pat, base + r, CH_HAT, C4, HAT, vol=hat_vol if r % 4 else min(64, hat_vol + 16))


def put_pad(s, pat, bar, chord, length=16, row0=0, ins=PAD, vol=None):
    base = row0 + bar * 16
    for k, n in enumerate(CHORDS[chord][1]):
        s.put(pat, base, CH_PAD0 + k, note(n), ins, vol=vol)
        if length < 16:
            s.put(pat, base + length, CH_PAD0 + k, NOTE_OFF)


def put_bass(s, pat, bar, chord, figure, row0=0):
    """figure: list of (row, semitones above the root, octave shift)."""
    root = note(CHORDS[chord][0])
    base = row0 + bar * 16
    for r, semis, octv in figure:
        s.put(pat, base + r, CH_BASS, root + semis + 12 * octv, BASS)


def put_melody(s, pat, notes, ch=CH_LEAD, ins=LEAD, row0=0):
    """notes: list of (row, 'A-4' or None for key-off)."""
    for r, n in notes:
        s.put(pat, row0 + r, ch, note(n) if n else NOTE_OFF, ins if n else 0)


def phrase(spec, step=1):
    """'A4:4 D5:4 C5:2 A4:2 G4:2 F4:2' -> [(row, 'A-4'), ...]; '-' rests.
    Durations are in rows (16ths)."""
    out, row = [], 0
    for tok in spec.split():
        name, dur = tok.split(":")
        if name != "-":
            if len(name) == 2:
                name = name[0] + "-" + name[1]
            out.append((row, name))
        else:
            out.append((row, None))
        row += int(dur) * step
    return out


# ---------------------------------------------------------------------------
# Songs

def song_mission():
    s = Song("Drop Zone", 132)
    prog_a = ["Dm", "Dm", "Bb", "C"]
    prog_b = ["Dm", "Dm", "Gm", "A"]
    ostinato = [(0, 0, 0), (2, 0, 0), (4, 12, 0), (6, 0, 0), (8, 0, 0), (10, 0, 0), (12, 12, 0), (14, 7, 0)]
    beat = dict(kicks=[0, 8, 10], snares=[4, 12], hats=[0, 2, 4, 6, 8, 10, 12, 14])
    beat16 = dict(kicks=[0, 6, 8, 10], snares=[4, 12], hats=list(range(16)))

    def section(prog, pad=True, lead=None, drums=beat, fill=False):
        p = s.new_pattern()
        for bar, ch in enumerate(prog):
            put_bass(s, p, bar, ch, ostinato)
            d = dict(drums)
            if fill and bar == 3:
                d = dict(kicks=[0, 8], snares=[4, 10, 12, 13, 14, 15], hats=[0, 2, 4, 6, 8])
            put_drums(s, p, bar, **d)
            if pad:
                put_pad(s, p, bar, ch)
        if lead:
            put_melody(s, p, phrase(lead))
        return p

    lead_a = ("A4:4 D5:4 C5:2 A4:2 G4:2 F4:2 "
              "E4:4 F4:2 G4:2 A4:8 "
              "Bb4:4 A4:2 G4:2 F4:4 D4:4 "
              "E4:4 G4:4 C5:6 -:2")
    lead_b = ("D5:4 F5:2 E5:2 D5:2 C5:2 A4:4 "
              "Bb4:4 A4:2 G4:2 F4:2 E4:2 F4:4 "
              "G4:4 Bb4:2 A4:2 G4:2 F4:2 E4:4 "
              "A4:4 C#5:4 E5:4 A5:2 -:2")
    intro_a = section(prog_a, pad=False)
    intro_b = section(prog_b, pad=True, fill=True)
    main_a = section(prog_a, lead=lead_a)
    main_b = section(prog_b, lead=lead_b, fill=True)
    drive_a = section(prog_a, drums=beat16)
    drive_b = section(prog_b, drums=beat16, lead=lead_b, fill=True)
    s.put(main_a, 0, CH_SNARE, C4, CRASH)  # a crash as the lead comes in
    s.order = [intro_a, intro_b, main_a, main_b, drive_a, drive_b, main_a, main_b]
    s.restart = 2
    return s


def song_hold():
    s = Song("Hold the Line", 150)
    prog = ["Dm", "Bb", "Gm", "A"]
    pulse = [(r, 0, 0) for r in range(0, 16, 2)] + [(r, 12, 0) for r in range(1, 16, 4)]
    pulse.sort()

    def section(lead=None, stabs=False, crash=False):
        p = s.new_pattern()
        for bar, ch in enumerate(prog):
            put_bass(s, p, bar, ch, pulse)
            put_drums(s, p, bar, kicks=[0, 4, 8, 12], snares=[4, 12] + ([14, 15] if bar == 3 else []),
                      hats=list(range(16)), hat_vol=34)
            if stabs:
                for r in (2, 6, 10):
                    for k, n in enumerate(CHORDS[ch][1]):
                        s.put(p, bar * 16 + r, CH_PAD0 + k, note(n), BRASS)
            else:
                put_pad(s, p, bar, ch)
        if lead:
            put_melody(s, p, phrase(lead))
        if crash:
            s.put(p, 0, CH_SNARE, C4, CRASH)
        return p

    siren = "A4:8 Bb4:8 A4:8 G4:8 A4:8 Bb4:8 C#5:8 E5:8"
    climb = "D5:4 E5:4 F5:8 D5:4 F5:4 G5:8 Bb4:4 D5:4 G5:8 A5:8 C#5:8"
    a = section()
    b = section(lead=siren, crash=True)
    c = section(stabs=True, lead=climb)
    d = section(stabs=True, lead=siren, crash=True)
    s.order = [a, b, c, d]
    s.restart = 0
    return s


def song_title():
    s = Song("Hivebreakers", 92)
    prog = ["Dm", "Bb", "F", "C", "Gm", "Dm", "Bb", "A"]

    def arp(p, bar, chord, row0=0):
        tones = [note(n) + 12 for n in CHORDS[chord][1]]
        seq = [tones[0], tones[1], tones[2], tones[1]] * 2
        for k, n in enumerate(seq):
            s.put(p, row0 + bar * 16 + k * 2, CH_LEAD, n, BELL, vol=40 if k % 2 else 52)

    patterns = []
    for half in range(2):
        for melody in (False, True):
            p = s.new_pattern()
            for bar in range(4):
                ch = prog[half * 4 + bar]
                put_pad(s, p, bar, ch)
                put_bass(s, p, bar, ch, [(0, 0, 0), (12, 7, 0)])
                if melody:
                    put_drums(s, p, bar, kicks=[0, 10], snares=[8], hats=[4, 12], hat_vol=24)
                else:
                    s.put(p, bar * 16, CH_KICK, C4, KICK, vol=40)
                    arp(p, bar, ch)
            if melody:
                tune = ("D5:8 C5:4 A4:4 F4:8 G4:4 A4:4 C5:8 A4:4 F4:4 G4:12 -:4"
                        if half == 0 else
                        "Bb4:8 D5:4 G4:4 A4:8 F4:4 D4:4 F4:8 Bb4:4 D5:4 C#5:12 -:4")
                put_melody(s, p, phrase(tune))
            patterns.append(p)
    s.order = patterns
    s.restart = 0
    return s


def song_victory():
    s = Song("Extracted", 120)
    p = s.new_pattern(48)
    fan = "D4:2 F#4:2 A4:2 D5:6 A4:2 D5:2 F#5:12 -:20"
    put_melody(s, p, phrase(fan), ins=BRASS)
    put_pad(s, p, 0, "D", length=40)
    s.put(p, 0, CH_BASS, note("D-2"), BASS)
    s.put(p, 12, CH_BASS, note("A-1"), BASS)
    s.put(p, 16, CH_BASS, note("D-2"), BASS)
    s.put(p, 40, CH_BASS, NOTE_OFF)
    for r in (0, 4, 8, 12):
        s.put(p, r, CH_KICK, C4, KICK)
    s.put(p, 16, CH_SNARE, C4, CRASH)
    s.order = [p]
    return s


def song_defeat():
    s = Song("Squad Lost", 76)
    p = s.new_pattern(48)
    put_melody(s, p, phrase("D5:4 C5:4 Bb4:4 A4:4 G4:4 F4:4 E4:4 D4:16 -:4"))
    put_pad(s, p, 1, "Dm", length=28)
    s.put(p, 16, CH_BASS, note("D-1"), BASS)
    s.put(p, 44, CH_BASS, NOTE_OFF)
    s.put(p, 16, CH_KICK, C4, KICK, vol=50)
    s.order = [p]
    return s


# ---------------------------------------------------------------------------
# Sound effects (22050 Hz mono)

SR = 22050


def fx_gun(seed, dur, tone_hz, noise_decay, tone_decay, darkness):
    n = int(SR * dur)
    crack = mul(lowpass(noise(n, seed), darkness), env_ad(n, SR, 0.0005, noise_decay))
    thump = sweep_tone(SR, dur, tone_hz * 2, tone_hz, curve=0.01, amp_decay=tone_decay)
    return normalize(mix(crack, scale(thump, 0.7)))


def make_sfx():
    sfx = {}
    sfx["rifle"] = fx_gun(101, 0.12, 110, 0.025, 0.03, 0.55)
    sfx["shotgun"] = fx_gun(102, 0.30, 70, 0.07, 0.08, 0.35)
    sfx["mg"] = fx_gun(103, 0.10, 130, 0.02, 0.025, 0.7)
    n = int(SR * 1.0)
    rumble = mul(lowpass(noise(n, 104), 0.08), env_ad(n, SR, 0.004, 0.32))
    boom = sweep_tone(SR, 1.0, 90, 30, curve=0.12, amp_decay=0.25)
    sfx["explosion"] = normalize(mix(rumble, scale(boom, 0.8)))
    n = int(SR * 0.45)
    sfx["impact"] = normalize(mix(sweep_tone(SR, 0.45, 70, 28, curve=0.06, amp_decay=0.12),
                                  mul(lowpass(noise(n, 105), 0.2), env_ad(n, SR, 0.001, 0.06))))
    n = int(SR * 0.06)
    sfx["bug_hit"] = normalize(mul(lowpass(noise(n, 106), 0.35), env_ad(n, SR, 0.0005, 0.012)), 0.6)
    sfx["armor_hit"] = normalize(mix(sweep_tone(SR, 0.12, 1900, 1500, curve=0.03, wave="triangle", amp_decay=0.03),
                                     scale(sfx["bug_hit"], 0.5)), 0.6)
    n = int(SR * 0.22)
    squelch = []
    src = noise(n, 107)
    y, k = 0.0, 0.5
    for i, x in enumerate(src):  # a closing filter: the splat
        k = 0.5 * math.exp(-i / (SR * 0.05)) + 0.03
        y += k * (x - y)
        squelch.append(y)
    sfx["bug_die"] = normalize(mix(mul(squelch, env_ad(n, SR, 0.002, 0.07)),
                                   scale(sweep_tone(SR, 0.22, 420, 120, curve=0.05, wave="square", amp_decay=0.05), 0.25)), 0.8)
    sfx["player_hurt"] = normalize(sweep_tone(SR, 0.14, 300, 160, curve=0.04, wave="square", amp_decay=0.05), 0.6)
    sfx["player_die"] = normalize(sweep_tone(SR, 0.6, 400, 70, curve=0.2, wave="square", amp_decay=0.25), 0.7)
    n = int(SR * 0.03)
    sfx["dry_fire"] = normalize(mul(highpass(noise(n, 108), 0.3), env_ad(n, SR, 0.0005, 0.004)), 0.5)
    click = sfx["dry_fire"]
    sfx["reload"] = mix(click, [0.0] * int(SR * 0.12) + scale(click, 0.9))
    sfx["code_beep"] = normalize(mul(sweep_tone(SR, 0.06, 1320, 1320, wave="triangle"),
                                     env_ad(int(SR * 0.06), SR, 0.002, 0.03)), 0.45)
    sfx["code_reject"] = normalize(mul(sweep_tone(SR, 0.18, 140, 120, wave="square"),
                                       env_ad(int(SR * 0.18), SR, 0.002, 0.08)), 0.45)
    sfx["beacon"] = normalize(mul(chirp(SR, 0.35, 600, 2200), env_ad(int(SR * 0.35), SR, 0.01, 0.12)), 0.5)
    n = int(SR * 0.7)
    whoosh = []
    src = noise(n, 109)
    y = 0.0
    for i, x in enumerate(src):  # an opening filter: something falling in
        k = 0.02 + 0.4 * (i / n) ** 2
        y += k * (x - y)
        whoosh.append(y)
    sfx["incoming"] = normalize(mul(whoosh, [min(1.0, i / (n * 0.8)) for i in range(n)]), 0.6)

    def chime(notes, step, decay=0.25, total=None):
        total = total or step * len(notes) + decay * 3
        out = [0.0] * int(SR * total)
        for k, hz in enumerate(notes):
            tone = mul(sweep_tone(SR, decay * 3, hz, hz, wave="sine"), env_ad(int(SR * decay * 3), SR, 0.003, decay))
            tone = mix(tone, scale(mul(sweep_tone(SR, decay * 3, hz * 3, hz * 3), env_ad(int(SR * decay * 3), SR, 0.003, decay / 2)), 0.2))
            off = int(SR * step * k)
            for i, v in enumerate(tone):
                if off + i < len(out):
                    out[off + i] += v
        return normalize(out, 0.55)

    sfx["pickup"] = chime([880, 1320], 0.07, 0.12)
    sfx["zone"] = chime([587, 740, 880, 1175], 0.09, 0.3)          # D major arpeggio
    sfx["menu_move"] = normalize(mul(sweep_tone(SR, 0.04, 900, 900, wave="square"),
                                     env_ad(int(SR * 0.04), SR, 0.001, 0.01)), 0.3)
    sfx["menu_select"] = chime([660, 990], 0.05, 0.08)
    # A radio blip: two short beeps through a crude band-limited "radio".
    beeps = mix(sweep_tone(SR, 0.07, 1800, 1800, wave="square"),
                [0.0] * int(SR * 0.1) + sweep_tone(SR, 0.07, 1500, 1500, wave="square"))
    sfx["radio"] = normalize(highpass(lowpass(beeps, 0.3), 0.05), 0.4)
    n = int(SR * 0.9)
    siren = [math.sin(2 * math.pi * (700 + 250 * math.sin(2 * math.pi * 2.2 * i / SR)) * i / SR) for i in range(n)]
    sfx["alarm"] = normalize(mul(siren, env_ad(n, SR, 0.05, 0.6)), 0.35)
    return sfx


# ---------------------------------------------------------------------------

def main():
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(1)
    out = sys.argv[1]
    os.makedirs(os.path.join(out, "music"), exist_ok=True)
    os.makedirs(os.path.join(out, "sfx"), exist_ok=True)
    instruments = make_instruments()
    for name, song in (("title", song_title()), ("mission", song_mission()), ("hold", song_hold()),
                       ("victory", song_victory()), ("defeat", song_defeat())):
        song.write(os.path.join(out, "music", name + ".xm"), instruments)
    for name, data in make_sfx().items():
        write_wav(os.path.join(out, "sfx", name + ".wav"), SR, data)


if __name__ == "__main__":
    main()
