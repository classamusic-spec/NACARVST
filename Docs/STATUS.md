# NÁCAR — what is finished and what is not

*Updated at the end of the Phase 9–17 build: Memory, modulation, the FX chain,
the atmosphere modules, Weight, and the routing that joins them.*

This document exists because the build specification forbids claiming a
subsystem works when it does not. Read it before assuming anything below is
production-ready.

---

## Summary

| Phase | Subject | State |
|---|---|---|
| 1 | Foundation: CMake, JUCE, processor/editor, parameters, state | **Complete** |
| 2 | The locked interface | **Complete** |
| 3–6 | Synth core: voices, oscillators, characters, richness | **Built and measured, not auditioned** |
| 7 | Synth performance: mono, legato, glide, expression | **Built, not auditioned** |
| 8 | Synth quality gate: benchmarks | **29/29 pass; not accepted by ear** |
| 9 | Memory: four generations | **Built and measured, not auditioned** |
| 10 | Modulation: LFOs, Breath, the matrix | **Built, measured and live** |
| 11 | Pulse | **Built; two of five destinations consumed** |
| 12 | Retro / Crush / Filter FX | **Built and measured, not auditioned** |
| 13 | Rewind | **Built; repeats rather than one-shots** |
| 14 | Grain | **Built and measured, not auditioned** |
| 15 | Space / Aura / Shadow / Patina | **Built and measured, not auditioned** |
| 16 | Weight | **Built and measured, not auditioned** |
| 17 | FX routing and locking | **Complete: order, bypass and DSP all real** |
| 18 | Sample import and waveform | **UI complete, decode not started** |
| 19 | Audio analysis | Not started |
| 20 | Harmony engine | Not started |
| 21 | Mutation engine | **State complete, engine not started** |
| 22 | Print / generations | Not started |
| 23 | Make instrument | Not started |
| 24 | Internal sound-design tools | Benchmark renderer and stage attribution |
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

**The chain processes.** Eleven engines behind the synth, in the order the
specification sets out:

```
SOURCE → MEMORY → [ six user-orderable FX ] → SHADOW → AURA → PATINA → WEIGHT → output
                    Retro Crush Filter
                    Rewind Grain Space
```

Every one of them reads its own parameters and changes the sound. The six FX
slots are reordered by dragging a card, and that order — packed into a single
32-bit word with the bypass mask and published with one relaxed store — is what
the audio thread actually runs.

**Every engine is called on every block, whether it is on or not.** Each one
manages its own bypass internally. This is not an oversight: Retro and Crush
have to keep their delay lines fed or the power button clicks; Space, Aura and
Shadow have to see the transition or they never flush their tails; Rewind and
Grain need their history written continuously or engaging them plays silence.
Gating the calls at the chain level took the silent-chain test from −10.7 dB to
−145.9 dB when it was removed.

The card's mute glyph works the same way. It used to skip the call — the same
mistake, made a second time through a different control — so muting a card and
switching it off produced different audio and different reported latency. The
chain now applies the mute by forcing the module's own power flag, so there is
one code path. Two tests assert the two routes agree: to −120 dB in the audio,
and exactly in the latency the chain reports.

**The modulation is live.** Two LFOs, Breath, Pulse and an Organic Random
source, all at sample rate, plus an eight-slot matrix whose routings now reach
the audio. A routing moves the parameter it targets, releases it when the
routing is removed, and is clamped into the parameter's own range —
`Source/Audio/Modulation/README.md` describes the overlay that makes that work
without every engine having to know the matrix exists.

**Memory has four generations, and they differ in kind.** Generation N runs N
complete copies in series rather than one copy turned up, and the character
table escalates per pass. Because each copy injects its noise early and every
later copy re-copies it, a third-generation copy is not a first-generation copy
with more of the same — which is the whole point of the control.

---

## What is UI and state only

Three things, and they are now the exceptions rather than the rule:

- `Source/Analysis/` — every analysis field in the session tree is still
  default. Nothing measures a sample.
- `Source/Mutation/` — recipes are generated and stored, nothing renders them.
  PRINT and MAKE INSTRUMENT say plainly in the interface that they are waiting
  on that engine rather than pretending to work.
- The **sequencer** (SEQ page) persists four lanes of sixteen steps with values,
  gates, lengths and targets. Nothing advances them against the host clock. Its
  PREVIEW playhead is an editing aid driven by the UI timer and is labelled as
  such on the page.

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

`NacarBench` renders 29 engineering benchmarks offline — as the bare synth, and
with `--chain`, through all eleven engines — and measures aliasing, mono
retention, low-band L/R correlation, spectral balance, crest factor, DC and CPU
against the specification's own thresholds. **All 29 pass in both modes.**

`NacarTests` renders at 44.1 / 48 / 88.2 / 96 kHz across every supported block
size and asserts the engine stays finite and bounded, that extreme resonance and
drive do not blow the filters up, that the sub survives a mono fold, that
all-notes-off actually silences it, that voice stealing does not explode, that
every FX order is stable, that a fully disabled chain is transparent, and that a
mod-matrix routing changes the rendered audio. **3,111,827 assertions pass, 0
fail.**

Headline numbers, through the whole chain at its defaults:

| | |
|---|---|
| Threshold violations, 29 patches | **0** |
| Mono retention, worst of 29 | −1.2 dB |
| Mono retention, every bass and Reese patch | −0.1 dB or better |
| Low-band L/R correlation, every bass and Reese patch | 0.98 – 1.00 |
| DC offset, worst of 29 | 0.0001 |
| Peak level, loudest of 29 | −5.3 dBFS |
| Peak level, INIT patch | −12.7 dBFS |
| Silent chain vs. dry | −145.9 dB |
| CPU, 1 voice, whole chain | 12.3 % of one core |
| CPU, 16 voices × 4 unison, whole chain | 62.3 % of one core |
| CPU, the chain's own fixed cost | ≈ 9.5 % of one core per instance |

Bare synth, for comparison: 2.9 % of a core for one voice, 52.7 % at 16 × 4,
and 126.6 % at 32 × 8 — which is over realtime and is stated as a limitation
below rather than rounded down.

**Measurement found defects that reading the code did not.** A filter applied as
a per-sample gain ratio taken from the mono sum; a subsonic pile-up in the
phase-modulated triangle; a half-sample misalignment in the oversampling
halfband; a +2.1 dB bump at 300 Hz in Memory from recombining bands that the
decorrelation had delayed unequally; decorrelation allpasses whose zero
coefficient made them plain delays, offsetting the channels by up to nine
samples even with decorrelation off. All five produced plausible-looking audio.

`NacarBench --attribute` re-renders a patch adding one chain stage at a time and
reports what each stage did to the stereo image. It exists because the
alternative — inferring which of eleven engines widened a patch — is guessing.

Every subsystem has a README that documents what it does and what is weak, as
specification §162 requires:
`Source/Audio/Sources/Synth/README.md`, `Source/Audio/Memory/README.md`,
`Source/Audio/Modulation/README.md`, `Source/Audio/FX/README.md`,
`Source/Audio/Atmosphere/README.md`, `Source/Audio/Weight/README.md`.

**Those are measurements, not a verdict.** The acceptance standard (§163) is
about how it *sounds*: whether the INIT patch feels premium, whether MASS bass
stays huge without reverb, whether HAZE chords are rich, whether MIRAGE is
modern without being harsh. Nobody has listened to this build. Until someone
does, Phase 8 is **measured but not accepted**, and Phase 25 onward should not
begin.

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
- **Latency reporting has not been checked against a host.** The chain declares
  Retro's, Crush's and Memory's onset delay through `AsyncUpdater`, and Memory's
  figure *changes at runtime* with the generation. A host that reads latency
  only at `prepareToPlay` will be wrong until it re-reads. Nobody has tried one.
- **CPU has been measured but not profiled in a session.** `NacarBench --cpu`
  and `--cpu --chain` give per-configuration numbers on one machine; nobody has
  run several instances in a DAW.
- **The 20 golden benchmark patches are engineering probes, not presets.** They
  exist to exercise the engine's corners, not to be shipped.

---

## Known limitations

Subsystem-level limitations live next to the code that has them, in the six
READMEs listed above. The ones that matter at instrument level:

**DSP**

- **32 voices at 8× unison exceeds realtime** — 137 % of one core through the
  chain. SIMD in the oscillator inner loop is the obvious missing optimisation;
  there is none anywhere in the build.
- **Memory adds up to 12.3 ms of latency at generation IV** and is not
  internally compensated. See *Not verified* above.
- **ULTRA quality is accepted and stored but behaves as STUDIO.** 4×
  oversampling is not implemented.
- **The mod matrix is block-rate.** One value per parameter per block, so a
  routing that sweeps fast enough steps at buffer boundaries. The destinations
  where that would be audible — the synth's own LFO paths, Pulse's envelopes —
  read per-sample buffers directly and do not go through the matrix.
- **Four matrix sources read zero**: ENV 1, ENV 2, VELOCITY and KEY TRACK. They
  are per-voice quantities, and making them work means the *voice* consulting
  the matrix rather than the engine publishing a global average. A test asserts
  they stay inert, so anybody who later wires a stand-in has to say so.
- **Three of Pulse's five envelopes are generated and discarded.** VOLUME and
  WIDTH are consumed by the output stage; FILTER, SPACE and MEMORY would each
  have to be read by the engine that owns that behaviour, and none does yet.
- **Pulse's SIDECHAIN source has no input.** NÁCAR is an instrument with no side
  bus, so the transient detector behind it has never processed a sample.
  Selecting SIDECHAIN falls back to CLOCK.
- **Rewind has no one-shot trigger.** `ParameterList.h` has no trigger
  parameter, so `rewind_on` is treated as a *repeat enable*: the gesture
  re-fires on the musical grid when synced, every `rewind_length` seconds when
  not, and on a Pulse trigger. It is a defensible reading of §88 — the power
  button behaves like a trigger for a user who taps it — but it is not the
  momentary control the specification describes, and adding one means a new
  permanent parameter ID.

**Interface**

- `SegmentedControl`, `ToggleSwitch` and `GenerationSelector` read their
  parameter inside `paint()` rather than on their own clock. The editor sweeps
  the chassis every fourth timer tick to compensate, so they follow automation
  at about 7 Hz rather than 30.
- `NacarKnob` places its label a fixed distance below the cap. The per-knob
  label baselines transcribed from the reference vary by a few pixels; the
  uniform gap is used instead, on the grounds that the variation is more likely
  to be transcription error than design intent. Worth checking against the
  image.
- The deep-edit pages (MOD, FX, SEQ, MIX) are new surfaces with no reference
  image behind them. They follow the chassis and the material language, but
  their internal layout is a design decision rather than a transcription.
- **Knobs show the user's value, not the modulated one.** There is no modulation
  ring: a parameter an LFO is sweeping looks identical to one that is still.
  That is the correct default — a cap that jitters cannot be read or typed into
  — but the missing indicator is a real gap.
- The sequencer stores step data that nothing reads yet.

---

## The next thing to do

Listen to `NacarBench`'s output — it writes one WAV per benchmark, and
`--chain` writes the same patches through the whole instrument. The
specification gates everything after Phase 8 on the synth sounding premium with
all atmospheric processing disabled, and that judgement cannot be made from a
table of numbers.

After that, in the specification's own order: sample decoding and analysis
(Phases 18–19), the harmony engine (20), and then the mutation engine (21),
which is the piece the whole second half of the instrument is waiting on. The
interfaces and the persisted state for all of them already exist.
