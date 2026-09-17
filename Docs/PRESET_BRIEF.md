# The NÁCAR factory library — how to write a preset

This is the working brief for anyone adding presets. It is also the standard
they are reviewed against.

---

## 1. Where the work goes

One category, one file, under `Source/Presets/Factory/`:

| File | Category word | Target count |
|---|---|---|
| `Keys.cpp` | `KEYS` | 24 |
| `Pads.cpp` | `PADS` | 28 |
| `Plucks.cpp` | `PLUCKS` | 26 |
| `Bells.cpp` | `BELLS` | 20 |
| `Leads.cpp` | `LEADS` | 30 |
| `Bass.cpp` | `BASS` | 32 |
| `Sub.cpp` | `SUB` | 20 |
| `Vocal.cpp` | `VOCAL-LIKE` | 20 |
| `Texture.cpp` | `TEXTURE` | 18 |
| `Atmosphere.cpp` | `ATMOSPHERE` | 20 |
| `Drums.cpp` | `DRUMS` | 22 |
| `Percussion.cpp` | `PERCUSSION` | 18 |
| `Sequences.cpp` | `SEQUENCES` | 22 |

**300 presets.** Nothing outside your own file may be edited. The shared
vocabulary lives in `Factory/PresetBuilder.h` and is read-only to you: if you
need a choice constant that is not named there, say so in your report rather
than writing a bare number.

---

## 2. What the library is for

A NÁCAR preset should sound like it came off a record, not off a demo reel.
The centre of gravity is **modern urbano** — the palette of Tainy, Sky
Rompiendo and Nelly El Arma Secreta, sounds that would sit in a Bad Bunny
record without being re-produced first — and the library reaches outward from
there into every genre the instrument can serve.

Concretely, the sounds the library is short of:

- **Mysterious leads.** Not bright and not thin. A lead that carries a hook
  over an empty mix: weight underneath it, air above it, and something
  unresolved in the middle — detune that beats slowly, a filter that is not
  fully open, a delay that answers rather than smears.
- **Mystical plucks.** The dembow top line. Short, tuned, with a tail that is
  reverb or delay rather than release. Bell-adjacent without being a bell.
- **Ethereal pads.** Wide, slow, and still legible under a vocal. Movement
  that you notice over four bars, not over four hundred milliseconds.
- **Thick Reese basses.** Two detuned saws beating against each other, kept
  mono where it matters (see §5), with real harmonic content between 80 Hz
  and 400 Hz so the bass survives a phone speaker.
- The hardware character the instrument is capable of: the **Virus TI**'s
  hyper-saw stacks and its hard, clean filter; the **OP-X**'s wide, slightly
  unstable analogue poly; the **Minimoog**'s fat, drive-led mono low end.

Reach for range, not for one idea repeated. Within your category, vary:

- the **oscillator pair** and the wavetable family under it,
- the **filter model** (`ch::fMass`, `ch::fHaze`, `ch::fComb`, `ch::fFormant`)
  and whether the creative second filter runs at all,
- the **FX chain order**, which *is* DSP order — ageing a reverb is not the
  same instrument as reverberating an aged sound,
- the **envelope shape**, which is most of what separates a pluck from a key
  and a lead from a pad.

A category where every preset shares a filter model and a chain order is one
patch at N cutoffs, and the bench will show it as a cluster of near-duplicate
pairs.

---

## 3. The mechanical contract

```cpp
add (out, Build ("Name", "CATEGORY", "MOOD",
                 "tag,tag,tag",                       // two to four, lower case
                 "The one sentence that is its identity.")
    .at (midiNote, chordNotes, velocity, seconds)     // how it gets auditioned
    .chain ("RETRO,CRUSH,FILTER,REWIND,GRAIN,SPACE")  // omit for the default
    .v (PID::someParam, value)                        // in the parameter's own units
    .route (srcLfo1, PID::someTarget, depth));        // bipolar -1..1
```

- **Category** is exactly one of: `KEYS PADS PLUCKS BELLS LEADS BASS SUB
  VOCAL-LIKE TEXTURE ATMOSPHERE DRUMS PERCUSSION SEQUENCES`.
- **Mood** is exactly one of: `DARK INTIMATE BROKEN NOSTALGIC AIRY AGGRESSIVE
  ROMANTIC COLD WARM CINEMATIC DIRTY DREAMY HAUNTED LUSH MINIMAL MYSTERIOUS`.
- **Names are unique across the whole library**, not just your file. Check
  before you commit to one.
- **Values are in the parameter's real units** — Hz, seconds, dB, 0..1 — and
  must be inside the min/max declared in `Source/Plugin/ParameterList.h`.
  Read that file. It is the only authority on a parameter's range, default
  and meaning, and the tooltip on each line says what the engine does with it.
- **CHOICE parameters are set with a named constant from `ch::`**, never a
  bare float. `.v (PID::filterModel, 2.0f)` is not a reviewable statement;
  `.v (PID::filterModel, ch::fComb)` is.
- **Routings name one of the ten global sources only**: `srcLfo1`, `srcLfo2`,
  `srcBreath`, `srcPulse`, `srcOrganic`, `srcMemory`, `srcMotion`, `srcWorld`,
  `srcWheel`, `srcTouch`. The four per-voice sources (ENV 1, ENV 2, VELOCITY,
  KEY TRACK) read zero in the global matrix — routing one of those would be a
  dead control, which §01 forbids. Eight routings maximum.
- **Say only what makes the preset itself.** Everything you do not set is the
  parameter list's default, applied by `PresetManager` before yours land. A
  preset that restates forty defaults is unreadable and hides its own idea.
- **The chain order string** must be a permutation of exactly
  `RETRO,CRUSH,FILTER,REWIND,GRAIN,SPACE`. An empty string means the default.

### Audition notes

`.at()` is a render hint, not a musical claim: it says which note the preset
was voiced around so the bench measures each one on material it was designed
for. A kick measured at C4 and a pad measured at C1 both read as broken when
neither is.

| Category | Typical note | Chord | Seconds |
|---|---|---|---|
| SUB, BASS | 24–36 | 1 | 2.5–4 |
| KEYS, PADS | 48–60 | 3–4 | 4–8 |
| PLUCKS, BELLS | 60–72 | 1–3 | 2.5–4 |
| LEADS | 60–72 | 1 | 3–5 |
| DRUMS, PERCUSSION | 36–48 | 1 | 1.5–2.5 |
| SEQUENCES | 48–60 | 1–2 | 6–8 |

---

## 4. The verification you must run

```
Tools/scripts/check-preset-file.sh Source/Presets/Factory/<YourFile>.cpp
```

Clean run prints nothing, exits 0. It compiles your file alone with the real
build's warnings as errors — no build directory is touched, so it is safe to
run while other people are working. **Do not run `cmake --build build`**: two
ninja invocations in one build directory truncate each other's output, and
that has already cost this project a day.

It proves your file compiles. It proves nothing about how it sounds.

---

## 5. The low-end rule, which is not negotiable

Master spec §38, §40, §43: **width is never bought at the cost of the low
end.** For `BASS` and `SUB` the bench enforces it — mono retention at or above
−1 dB and low-band correlation at or above 0.90 — and a preset that fails is
a defect, not a taste difference. In practice: keep stereo widening, wide
chorus, wide reverb and hard-panned detune off the fundamental. Put the width
above it.

Master spec §148: **do not use loudness to fake quality.** Peak must land
between −40 dBFS and 0 dBFS; a preset that is simply louder than its
neighbours is not a better preset, and the bench prints RMS next to every row
so this is visible.

---

## 6. What you may not claim

Nobody has listened to any of this. No DAW has loaded it. The blurb on a
preset describes **what it is made of and what it is for** — "two detuned saws
through the MASS filter with the sub an octave down" — and never asserts how
it sounds to a listener, never names a record it would suit as though that had
been checked, and never says it was auditioned, mixed or approved.

Write the truth: these are settings, chosen with intent, measured for level
and spectrum and nothing else.
