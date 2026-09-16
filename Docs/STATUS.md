# NÁCAR — what is finished and what is not

*Updated at the end of the Phase 1 / Phase 2 / synth-core build.*

This document exists because the build specification forbids claiming a
subsystem works when it does not. Read it before assuming anything below is
production-ready.

---

## Summary

| Phase | Subject | State |
|---|---|---|
| 1 | Foundation: CMake, JUCE, processor/editor, parameters, state | **Complete** |
| 2 | The locked interface | **Complete** |
| 3–6 | Synth core: voices, oscillators, characters, richness | **Built, not auditioned** |
| 7 | Synth performance: mono, legato, glide, expression | **Built, not auditioned** |
| 8 | Synth quality gate: 20 golden benchmarks | **Measured, not accepted** |
| 9 | Memory | Not started |
| 10 | Modulation / Breath | Parameters and UI only |
| 11 | Pulse | Parameters and UI only |
| 12 | Retro / Crush / Filter FX | Parameters and UI only |
| 13 | Rewind | Parameters and UI only |
| 14 | Grain | Parameters and UI only |
| 15 | Space / Aura / Shadow / Patina | Parameters and UI only |
| 16 | Weight / Alter | Parameters and UI only |
| 17 | FX routing and locking | **State complete, DSP not started** |
| 18 | Sample import and waveform | **UI complete, decode not started** |
| 19 | Audio analysis | Not started |
| 20 | Harmony engine | Not started |
| 21 | Mutation engine | **State complete, engine not started** |
| 22 | Print / generations | Not started |
| 23 | Make instrument | Not started |
| 24 | Internal sound-design tools | Benchmark renderer only |
| 25–27 | Golden presets, golden mutations, factory library | Not started |
| 28–29 | Optimisation, full QA | Not started |

---

## What genuinely works

**The plugin builds, loads and opens.** VST3 and Standalone on Linux; the same
CMake produces AU on macOS and VST3 on Windows, though neither has been built or
loaded in a host from this environment — see *Not verified* below.

**Every parameter is real.** 251 host parameters, each with a permanent ID, a
sensible range and skew, a default, a unit, a tooltip and host-correct value
formatting. They automate, they persist, they undo. The registry gives the audio
thread lock-free access with no string lookup.

**State round-trips.** Parameters and the whole session tree — sample identity,
FX order and locks, mutation seed and recipe history, preset metadata, editor
preferences — survive save and reload, tolerate truncated and corrupt blobs, and
migrate forward from an older schema without discarding unknown properties. This
is covered by tests.

**The interface is the locked reference.** Every region, control, material and
piece of type from the approved art direction, on a fixed 1536 × 1024 canvas
with one uniform transform for 75 % to 200 %. Knobs, pills, segmented controls,
switches, power rings, the FX cards and the atmosphere modules are all bound to
real parameters or to real persisted state. Drag, shift-fine, double-click
reset, typed value entry, context menu, wheel and tooltips work throughout.

**The synth makes sound.** Bandlimited oscillators, wavetables, unison with
correlation-aware normalisation, sub, noise, Body, Density, drift, voice
variation, two primary filter characters plus comb and formant, three envelopes,
poly/mono/legato with glide, voice stealing, and frequency-dependent stereo with
a mono low band. MIRAGE, HAZE and MASS share that core and differ in behaviour.

---

## What is UI and state only

The FX chain, the atmosphere modules, Memory, Pulse and the modulation matrix
have **complete interfaces and complete parameter and state plumbing, and no
DSP behind them yet.** Turning the AURA knob writes a real automatable parameter
that persists in the session; it does not currently change the sound.

This is the order the specification itself sets out (§156, §157): the synth must
be accepted before Memory and the effects ecosystem become the focus. It is
recorded here rather than hidden because a control that writes a parameter no
engine reads is easy to mistake for a working effect.

Specifically not yet consuming their parameters:

- `Source/Audio/Memory/` — the four generations
- `Source/Audio/FX/` — Retro, Crush, Filter, Rewind, Grain, Space
- `Source/Audio/Atmosphere/` — Aura, Shadow, Patina
- `Source/Audio/Weight/` — Sub / Body / Air
- `Source/Audio/Modulation/` — LFOs, Breath, Pulse, the matrix
- `Source/Analysis/` — every analysis field in the state tree is still default
- `Source/Mutation/` — recipes are generated and stored, nothing renders them

The FX chain's **order and locks are real**: dragging a card rewrites the
persisted order, and that order is what the DSP will read when it exists.
Likewise the mutation panel writes genuine seeds and recipe history that the
Phase 21 engine will consume — but PRINT and MAKE INSTRUMENT say plainly in the
interface that they are waiting on that engine rather than pretending to work.

Dropping an audio file records its path in the session tree. It does **not**
decode, analyse or play it — asynchronous decoding is Phase 18 and analysis is
Phase 19. The waveform view shows the instrument's own recent output level
instead, and labels it as such. No fake waveform is drawn for a file that has
not been read.

The preset browser's search, filters and list are wired against an empty
library and show an empty state. No placeholder preset names were invented to
make it look populated.

---

## What has been measured, and what has not

`NacarBench` renders 22 engineering benchmarks offline and measures aliasing
under sync/FM/unison/drive, mono retention, low-band L/R correlation, spectral
balance, crest factor and DC, against the specification's own thresholds.
`NacarTests` renders at 44.1 / 48 / 88.2 / 96 kHz across every supported block
size and asserts the engine stays finite and bounded, that extreme resonance and
drive do not blow the filters up, that the sub survives a mono fold, that
all-notes-off actually silences it, and that voice stealing does not explode.

**Those are measurements, not a verdict.** The specification's synth acceptance
standard (§163) is about how it *sounds*: whether the INIT patch feels premium,
whether MASS bass stays huge without reverb, whether HAZE chords are rich,
whether MIRAGE is modern without being harsh. Nobody has listened to this build.
Until someone does, Phase 8 is **measured but not accepted**, and Phase 25
onward should not begin.

---

## Not verified

Stated plainly, because the specification forbids claiming otherwise:

- **No DAW has loaded this plugin.** It has not been opened in Ableton, Logic,
  Bitwig, FL, Reaper or Pro Tools.
- **No audio has been auditioned.** The WAVs the benchmark renderer writes have
  not been listened to by anyone.
- **macOS and Windows have not been built.** The CMake is written for them and
  the code is platform-neutral, but only Linux has actually compiled.
- **AU has not been built or validated.** `auval` has not run.
- **No CPU profiling has been done.** Polyphony and unison costs are untested
  against a real session with several instances.
- **The 20 golden benchmark patches are engineering probes, not presets.** They
  exist to exercise the engine's corners, not to be shipped.

---

## Known limitations

Subsystem-level limitations are documented next to the code that has them —
see `Source/Audio/Sources/Synth/README.md` for the synth's, which covers the
oscillator and anti-aliasing strategy, the unison normalisation formula, the
filter architecture, the oversampling policy and what is weak.

Interface-level:

- `SegmentedControl`, `ToggleSwitch` and `GenerationSelector` read their
  parameter inside `paint()` rather than on their own clock. The editor sweeps
  the chassis every fourth timer tick to compensate, so they follow automation
  at about 7 Hz rather than 30.
- `NacarKnob` places its label a fixed distance below the cap. The per-knob
  label baselines transcribed from the reference vary by a few pixels; the
  uniform gap is used instead, on the grounds that the variation is more likely
  to be transcription error than design intent. Worth checking against the
  image.
- The mod matrix and the sequencer store routings and step data that nothing
  reads yet.

---

## The next thing to do

Listen to `NacarBench`'s output. The specification gates everything after
Phase 8 on the synth sounding premium with all atmospheric processing disabled,
and that judgement cannot be made from a table of numbers.
