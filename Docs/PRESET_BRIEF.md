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

## 3a. The two envelope destinations, and the trap in them

`pitch_env_amt` and `pm_env_amt` are newer than most of the library and easy
to miss, so they get their own note.

| | range | what it does |
|---|---|---|
| `pitch_env_amt` | −48…+48 st | mod envelope 2 into oscillator pitch |
| `pm_env_amt` | −1…+1 | mod envelope 2 into the PM index |

**The sign is the thing people get backwards.** Envelope 2 rises to 1 at
note-on and falls to its sustain, and the offset is `amount × env2`. So a
**positive** `pitch_env_amt` starts the note **high and falls onto it** — the
kick, the 808, the conga. A negative one arrives from below.

**The trap: envelope 2's defaults are slow** — attack 0.90 s, decay 1.50 s,
sustain 0.60. A preset that sets either amount and not the envelope gets a
lazy swell instead of a transient, which is the opposite of what it asked
for. Set `env2_attack`, `env2_decay` and `env2_sustain` every time.

Rough shapes: a kick 12–36 st over 20–60 ms; a tom 5–12 st over 60–150 ms; a
hand drum 3–10 st over 10–40 ms; a tine's index over 250–900 ms; a mallet's
over 50–300 ms. Judge each preset rather than copying a number.

**Two things that are not obvious:**

- Envelope 2 is *not* envelope 1, on purpose. Envelope 1 is the filter
  envelope, and a tine needs its index to collapse while its filter opens
  slowly. But envelope 2 already had two other destinations before these
  parameters existed: it adds a little to density, and it **morphs the vowel
  of the FORMANT filter model**. On a `ch::fFormant` preset, reshaping
  envelope 2 for a pitch or index envelope also changes how the vowel sweeps.
  That is a real trade, not a bug — make it deliberately.
- `pm_env_amt` is **summed** with `osc_pm` and the total is clamped to 0…1.
  A preset with a high fixed index *and* a high envelope sits pinned at the
  ceiling for the whole note, which wastes the envelope entirely. Lower the
  fixed index when you add one.

The filter sweep these replace is still a legitimate thing to do — it is just
no longer the only way to get a pitched fall or a decaying index, and a preset
should not credit the filter for something the pitch envelope is now doing.

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
