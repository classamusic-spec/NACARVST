# NÁCAR architecture

## The shape of it

```
                         ┌─────────────────┐
              MIDI ─────▶│ NacarProcessor  │
                         └────────┬────────┘
                                  │  audio thread only
                    ┌─────────────┼─────────────┐
                    ▼             ▼             ▼
              ParameterRegistry  Engines   Output stage
                    ▲                           │
                    │ lock-free atomics         ▼
                    │                        host buffer
              ┌─────┴──────┐
              │ NacarEditor│  message thread only
              └────────────┘
```

Two threads, one direction of travel. The editor writes parameters through the
host and reads them back through cached atomics; it never touches an engine.
The audio thread never touches a `juce::String`, a file, a lock or the heap.

## Parameters

Every host parameter is declared exactly once, in `Source/Plugin/ParameterList.h`,
through an X-macro:

```cpp
FLOAT (macroMemory, "macro_memory", "Memory", 0.0f, 1.0f, 0.22f, 1.0f, "%", "…")
```

From that single line the build generates:

- a `PID` enum value — the compile-time handle audio code uses
- the `AudioParameterFloat` in the APVTS layout, with its range, skew and
  value-formatting function
- an entry in the static `ParamDef` table
- a slot in the cached `std::atomic<float>*` array
- the tooltip the UI shows

They cannot drift apart, because there is only one of them.

**The string IDs are permanent.** A shipped ID is never renamed, never reused
for a different meaning, and never removed — a host has it in a session file and
an automation lane is pointing at it. Adding to the end of a group is always
safe. Retiring one means leaving it in place.

`ParameterRegistry::raw(PID)` returns the value in **real units** — Hz, seconds,
semitones, dB — not normalised. Audio code should not be doing range arithmetic.

## State

Host parameters live in the APVTS. Everything else lives in a versioned
`SESSION` tree owned by `StateManager`:

| Group | Holds |
|---|---|
| `PRESET` | name, author, category, mood, tags, favourite |
| `SAMPLE` | file, rate, length, selection, playhead, zoom |
| `ANALYSIS` | root, scale, tempo, transients, spectral measures, confidences |
| `FXCHAIN` | slot order (== DSP order), locks, selection |
| `MUTATION` | seed, seed lock, engine version, recipe history |
| `GENERATIONS` | print lineage |
| `EDITOR` | scale, page, browser state, per-module knob selection |
| `MACROS` · `MODMATRIX` | routings |

`StateManager::upgrade()` migrates older trees forward one version at a time and
never discards a property it does not recognise, so a session written by a newer
build degrades rather than losing data.

## The realtime contract

Inside anything reachable from `processBlock`:

> no heap allocation · no file IO · no mutex · no blocking · no networking ·
> no logging · no compressed decoding · no `juce::String`

Everything is allocated in `prepare()`. Parameters are read once per block and
smoothed. MIDI is handled with sample-accurate timing by rendering in sub-blocks
between events. Anything that must cross the thread boundary does so through an
atomic or a lock-free FIFO.

The output stage ends with a finite-and-bounded guard: NACAR never hands a host
a NaN or anything past full scale, whatever a mutation recipe asked for.

## The interface

The whole interface is laid out on a fixed **1536 × 1024 logical canvas** taken
from the locked reference image. `NacarEditor` applies one uniform
`AffineTransform` to reach the window size. Nothing reflows — it is a machined
front panel, not a responsive page.

Two files own everything:

- `Source/UI/Theme.h` — every colour, font and shading routine
- `Source/UI/Layout.h` — every coordinate

A component that constructs a colour from a literal, or that hard-codes a
position, is a bug. The `layout::` sub-namespaces are **region-local**, so a
panel positions its contents against its own origin and does not need to know
where it sits on the canvas.

Region components talk to the editor through `ui::EditorHost`, never by casting
to their parent.

### Widget vocabulary

`Source/UI/Components/Widgets.h` declares the shared controls: `NacarKnob`,
`PillButton`, `IconButton`, `PowerButton`, `SegmentedControl`, `ToggleSwitch`,
`PreserveLock`, `GenerationSelector`, `HairlineSlider`, and the ceramic/glass
panels. `ParamControl` gives all of them one drag, fine-drag, reset, type-value,
context-menu and tooltip behaviour, and one host gesture protocol.

Icons are vector paths authored in a 100 × 100 box, so they stay crisp at 200 %
and on HiDPI.

## Engines

```
Source/Audio/
  NacarEngine         the chain: owns every engine and routes between them
  MacroResolver       what the five macro knobs mean to the engines
  DspCommon.h         delay lines, allpasses, band splits, tilt, history
  EngineContext.h     EngineSpec, MacroState, the engine convention

  Sources/Synth/      MIRAGE / HAZE / MASS over one core
  Sources/            SampleEngine, GrainSource, ResonatorEngine, SpectralEngine
  Memory/             the four generations
  Modulation/         LFOs, Breath, Pulse, mod matrix
  FX/                 Retro, Crush, Filter, Rewind, Grain, Space
  Atmosphere/         Aura, Shadow, Patina
  Weight/             Sub / Body / Air
```

Every engine offers exactly three methods — `prepare(EngineSpec)`, `reset()`,
`process(buffer, registry, macros)` — and works in place on a stereo buffer.
There is no base class: a virtual call per engine per block would cost nothing,
but the uniformity is worth more as a rule people follow than as an interface
the compiler enforces, and it keeps each engine's header free of anything but
that engine.

### The chain

```
SOURCE        the synth, and in later phases the four other source engines
  → MEMORY       generational history
  → FX CHAIN     retro, crush, filter, rewind, grain, space,
                 in whatever order the user has put them
  → SHADOW       an atmospheric duplicate of the finished sound
  → AURA         the environment it all sits in
  → PATINA       the surface it has ended up with
  → WEIGHT       physical mass, last
  → OUTPUT       Pulse's volume and width destinations
```

Memory is first because it is about what the *source* has been through — after
the effects it would age the effects rather than the sound. Shadow duplicates
the finished sound rather than the raw one, or it would be a duplicate of
something nobody heard. Aura is the environment and so contains everything.
Weight is last because it is the only stage whose job is the finished thing's
physical size.

The order is reorderable at runtime, so it crosses the thread boundary as a
packed integer — six three-bit slot indices, a count and a bypass mask — written
by the message thread and unpacked by the audio thread at the top of each block.
The audio thread never reads a `ValueTree`.

### Macros

The five knobs on the left panel reach most of the instrument. `MacroResolver`
converts what they *say* into a handful of named concepts — `age`, `grit`,
`movement`, `scale`, `distance`, `wetBias`, `widthScale`, `alterAmount` — that
an engine consults and **adds** to its own settings, so a patch that sets a
control explicitly still wins.

There is deliberately no central table mapping macro to parameter. It would put
six engines' worth of voicing decisions in one file that nobody who works on
those engines ever opens. Each engine documents its own macro response next to
its code.

### Pulse

Specification §83: a kick makes a sound quieter **and** darker **and** narrower
**and** drier at once. So Pulse generates five differently *shaped* envelopes
from one trigger — the image recovers fastest, reverb tails stay masked longest
— rather than one envelope scaled five ways.

Volume and width are applied by the chain's output stage, because they are
properties of the finished sound. Filter, space and memory are applied by the
engines that own those behaviours, because a duck that darkens has to happen
where the darkening happens.

The synth is not three synthesizers. MIRAGE, HAZE and MASS share the entire
core — oscillators, voice allocation, modulation, envelopes, filters, unison,
voice mixer, gain architecture — and differ only in *behaviour*: unison
topology, drift and variation scaling, filter character, stereo distribution,
envelope personality, harmonic emphasis.

## Mutation

```
ANALYZE → BUILD CONSTRAINTS → GENERATE CANDIDATES → SCORE → REJECT → SELECT → ACTIVATE
```

HARMONY and DISTANCE are **independent axes**. Harmony governs how far a
mutation may stray harmonically (SAFE · COLOR · FREE); Distance governs how far
it may stray timbrally (NEAR · FAR · UNKNOWN). `FAR + SAFE` — a radical timbral
transformation that stays harmonically compatible — is a normal and useful
combination, and the design depends on it staying possible.

Randomness is always **weighted**, never uniform over a full range. Every
mutation stores its seed, so the same source, seed, engine version and settings
reproduce.

## Threading summary

| Thread | Owns |
|---|---|
| Audio | DSP only |
| Message | the entire interface |
| Background workers | sample decoding, waveform overviews, analysis, preset preparation, mutation preparation, offline rendering |
