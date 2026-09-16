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
  Sources/Synth/      MIRAGE / HAZE / MASS over one core
  Sources/            SampleEngine, GrainSource, ResonatorEngine, SpectralEngine
  Memory/             the four generations
  Modulation/         LFOs, Breath, Pulse, mod matrix
  FX/                 Retro, Crush, Filter, Rewind, Grain, Space
  Atmosphere/         Aura, Shadow, Patina
  Weight/             Sub / Body / Air
```

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
