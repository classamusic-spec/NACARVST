# The NÁCAR modulation layer

Everything downstream reads what this produces, so the output contract matters
more than any algorithm inside it.

```
ModulationEngine.h/.cpp   fills a MacroState's modulation fields each block
LFO.h/.cpp                seven shapes, free or locked to song position
BreathEngine.h/.cpp       organic non-repeating movement; also ORGANIC RANDOM
PulseEngine.h/.cpp        five differently shaped ducks from one trigger
ModMatrix.h/.cpp          eight routings, message thread to audio thread
```

`mod::Clock` and `mod::smoothStep` live at the top of `LFO.h` rather than in a
seventh header, because the file set for this directory is fixed and the clock
is first needed by the first thing that locks to tempo.

---

## The output contract

`updateBlock()` fills, for `MacroState::numSamples` samples:

| field | range | |
|---|---|---|
| `lfo1`, `lfo2` | −1 … 1 | per sample, already scaled by `lfoN_depth` |
| `breath` | −1 … 1 | per sample |
| `pulse` | 0 … 1 | per sample, 1 = fully ducked |
| `pulseToVolume/Filter/Space/Width/Memory` | 0 … 1 | already multiplied by `pulse_depth` |

All four buffers are allocated in `prepare()` at `EngineSpec::maxBlockSize`, and
are never null once `prepare()` has run. They are valid until the next
`updateBlock()`. Every sample goes through `fx::guard()` and then through an
explicit `jlimit` to the declared range, because a downstream engine is entitled
to index a table with `pulse` without checking it first.

**Per sample, not per block.** Pulse is a fast envelope — half a millisecond of
attack at the short end — and a block-rate version of it would step at every
buffer boundary, which is a click on exactly the transient the duck exists to
make room for. The LFOs and Breath are slow enough that a block-rate value would
survive, but they cost almost nothing per sample and a consumer should not have
to know which of the four is which.

### Macro response

One, deliberately. Motion lifts Breath's depth towards full:

```
breathAmount' = amount + 0.25 * movement * (1 - amount)
```

so Motion can only ever add. A patch that asked for Breath keeps it at Motion
zero, and Motion can never take breathing away from a patch that asked for it.

The LFOs have no macro response: they are explicit controls and the user's rate
and depth are the whole statement. Pulse has none either — it is a rhythmic
decision, not an amount of life.

`MacroState::widthScale` is documented in `EngineContext.h` as "World + Pulse",
and this engine leaves it exactly as the macro resolver wrote it. Pulse's width
destination is a per-sample envelope; folding a block-rate version of it into
`widthScale` would lose the shape *and* double-count against the chain's output
stage, which already applies `widthScale * (1 - pulse * pulseToWidth)` per
sample. The comment in the contract is describing the intent of the two together,
not asking this engine to pre-multiply them.

---

## The LFOs — §56

Seven shapes (SINE, TRIANGLE, SAW UP, SAW DOWN, SQUARE, RANDOM, SMOOTH RANDOM),
free-running in Hz or locked to a tempo division from `fx::kLfoDivisionBeats`,
with a 0…1 start-phase offset applied at evaluation rather than baked into the
accumulator (so turning the Phase knob offsets the wave instead of permanently
displacing it).

### Tempo sync locks to song position

When SYNC is on **and the transport is running**, the phase is computed from the
host's `ppqPosition` plus a per-sample increment — it is not accumulated:

```
cyclesAtSample(i) = (ppqPosition + i * beatsPerSample) / beatsPerCycle
```

A synced LFO that merely free-runs at the right rate drifts against the song the
moment the user loops, scrubs or drops the playhead, and the drift is silent
until it is the reason a bar does not sound the way it did yesterday. Deriving
the phase from song position makes a render identical from any start point.

The free-running accumulator is still written every sample with the song-derived
value, so **stopping the transport continues from exactly where the song left
it**. A stopped transport reports a ppq position that does not move, which is why
the song-locked path is conditional on `transportPlaying` — otherwise a synced
LFO would freeze at DC while the user is editing.

Free and synced share one increment expression, because
`1 / (beatsToSeconds(beats, bpm) · fs)` and `beatsPerSample / beats` are the same
quantity: there is no second code path to keep in step.

### The random shapes

RANDOM is sample-and-hold at the LFO rate; SMOOTH RANDOM interpolates between the
same held values with `smoothStep`. The held value is **a pure function of the
seed and the integer cycle index**, computed by seeding an `fx::Rng` with a mix of
the two:

```
randomAt(seed, step) = Rng(seed ^ low(step) ^ high(step)*0x85EBCA6B).nextBipolar()
```

That is what lets a *synced* random LFO hold the same values on every pass over
the same bar, with no state to resynchronise — the value is addressed by song
position, not remembered. It also means the session recalls identically, on any
machine, with no clock anywhere in the path.

### Slew, and why it is there

SQUARE, both SAWs and RANDOM contain genuine discontinuities. An LFO does not
alias the way an audio oscillator does — it is not summed into the signal, it
moves a parameter — but a parameter that steps is a filter cutoff that clicks. So
the four discontinuous shapes pass through a **1.2 ms one-pole**.

At the maximum 40 Hz rate that is 4.8 % of a cycle, and a square's plateau still
settles to better than 0.01 % of full scale. The cost is real and is stated here
rather than hidden: a fast SAW loses a little of its peak and gains about a
millisecond of lag. The three continuous shapes bypass the filter but keep its
state primed, so changing shape while the LFO runs cannot step.

---

## Breath — §57, §94, and why it is not an LFO

> "BREATH is not another LFO. BREATH creates organic non-repeating movement."

That rules out the two obvious answers. A slow LFO has a period and a listener
finds a period in a few seconds. A bounded random walk has no period but has to
be clamped, and a clamped walk spends most of its life against the rails, which
reads as a signal that keeps hitting the ends rather than as something alive.

`DriftGenerator` in the synth solved the small version of this with two sines at
an irrational ratio. Breath is the capable version of the same idea.

### Bounded by construction, not by clamping

**Five** sines whose weights are `0.40, 0.24, 0.16, 0.12, 0.08` — they sum to
exactly 1, so `|Σ wₖ sin(φₖ)| ≤ Σ wₖ = 1`. There is no clamp in the generator and
none is needed; if one were ever required the construction would be wrong.

The slowest component runs at SPEED. The other four run at SPEED × a ratio.

### RANDOM does two separate things

1. **It morphs the ratios.** At 0 they are *near* rational — `2.013, 3.027,
   5.041, 7.963` — which repeats only after a few hundred cycles of the slowest
   component, so the movement reads as almost periodic without actually being
   periodic. At 1 they are mutually irrational: φ, e, Feigenbaum's δ, e².
   Irrational ratios have no common period at all.

2. **It opens a rate jitter.** Each component's instantaneous rate is multiplied
   by `1 + 0.6·RANDOM·j`, where `j` is a deterministic random value interpolated
   with `smoothStep` between draws taken on a slow clock. Because the jitter
   modulates the *rate*, its effect on phase is the integral of a random
   sequence: the trajectory has no closed form and does not repeat until the
   generator's own 2³²-state cycle does. The multiplier stays in 0.4 … 1.6, so
   no component can stall or reverse — and the output is still a weighted sum of
   sines, so it is still bounded with no clamp.

### SHAPE — the second personality

A step clock (regular at RANDOM 0, increasingly irregular above it) samples the
continuous sum and glides to that value with a one-pole whose time constant is
28 % of the mean step interval, so it always arrives before the next decision.
SHAPE crossfades the continuous wander into that sample-and-glide: at 0 a slow
wander, at 1 something that reads as a decision being made.

This does not break the bound either. A one-pole cannot overshoot; its target is
a sample of a signal already in −1…1; and a crossfade of two signals in −1…1 is
in −1…1.

### Deterministic but not periodic

Those are different properties and both are required. Every random quantity comes
from an `fx::Rng` seeded from a constant in `prepare()`. Draws happen only when
one of the two clocks wraps — timing that depends on elapsed samples and the
parameter values, never on the block size. Two renders of the same session are
identical; neither of them repeats.

Two separate generators are used, one for the jitter and one for the step clock,
so the interleaving of their draws can never depend on their relative timing.

### Cost

The chain runs once every 16 samples and is linearly interpolated in between,
exactly as `DriftGenerator` does. At 44.1 kHz that is a 2756 Hz control rate for
a modulator whose fastest component tops out near 28 Hz. The interpolation is a
convex combination of two bounded values, so it cannot break the bound.

---

## Pulse — §82, §83

> §83: "A kick event may simultaneously make a sound quieter, darker, narrower
> and drier. This is more sophisticated than volume-only sidechain."

The MOD page already prints that under the destination knobs, so the interface is
promising the user this behaviour.

### The psychoacoustic model: five shapes, not one envelope scaled five ways

When something loud happens nearby, the ear does not simply turn the other sound
down. Three things happen at different speeds. High-frequency detail is masked
first and recovers first, because forward masking is shortest at high
frequencies. The stereo image collapses towards the centre and springs back
fastest of all, because localisation is re-established as soon as the interaural
cues are audible again. Reverb is masked longest, because a tail is quieter than
the direct sound that produced it and stays under the masker after the direct
sound has re-emerged.

So one trigger drives five envelopes:

| destination | attack × | release × | curve bias | why |
|---|---|---|---|---|
| VOLUME | 1.00 | 1.00 | 0.00 | the reference duck |
| FILTER | 0.80 | 0.70 | −0.15 | HF masked first, back first |
| WIDTH | 0.85 | 0.60 | −0.20 | the image recovers fastest |
| SPACE | 1.20 | 1.60 | +0.20 | tails stay masked longest |
| MEMORY | 1.00 | 1.30 | +0.10 | a texture change, not a gate |

The scalings multiply the user's ATTACK and RELEASE, so the relationship between
the five is fixed while the absolute speed stays the user's. The curve column is
a bias added to SMOOTH before clamping, so FILTER and WIDTH are always a little
snappier than VOLUME and SPACE and MEMORY are always a little softer.

The five rows above are the entire model, and they are the only place it is
expressed.

### The curves

Both stages run on an exact phase ramp rather than a one-pole aimed at an
asymptote, so a 2 ms attack is 2 ms at every sample rate and the stage ends at
exactly 1. SMOOTH morphs

* the **attack** from `1 − (1−p)²` (straight down into the duck) to `p²(3−2p)`;
* the **release** from `(1−p)²` (out quickly, then easing in) to `smoothStep(1−p)`,
  which has zero slope at the start and therefore holds the duck a moment longer
  before letting go.

Both functions are monotone and both end points are exact, so the output is 0…1
by construction. A trigger during the release restarts the attack **from the
current level**, so a fast division with a long release never steps.

### The three trigger sources

**CLOCK** — the host grid. The exact sample inside the block is computed from the
ppq position, not rounded to the block boundary:

```
k = ceil(startPpq / beats - ε)
while k*beats < endPpq:  trigger at (k*beats - startPpq) / beatsPerSample
```

The window is half-open `[start, end)` and the next block starts at this block's
end, so no beat is counted twice and none is missed — and because the position is
recomputed from ppq every block, a loop or a scrub needs no resynchronisation
state at all. When the transport is stopped an internal beat counter runs at the
host tempo so the user can audition Pulse without pressing play; it is kept level
with the song position while the transport runs, so stopping continues in place.

**MIDI** — `noteTriggered()`. `NacarEngine` calls it once per note-on, at the top
of the block. `noteTriggeredAt(offset)` exists for a caller that knows the note's
offset; nothing uses it yet, so MIDI-triggered Pulse is currently quantised to
the block, up to 21 ms at 48 kHz with a 1024-sample buffer.

**SIDECHAIN** — `setSidechainInput()` and a transient detector: a fast envelope
(0.5 ms / 40 ms) against a slow one (180 ms), firing when the fast one exceeds
2× the slow one plus a −54 dBFS floor, with Schmitt hysteresis at 1.25× so one
kick cannot fire five times on its way up. A ratio rather than an absolute
threshold, so it works at any input level and needs no sensitivity control.

**NOTHING CALLS `setSidechainInput`.** NÁCAR is an instrument with no side input,
so there is nowhere for the signal to come from. Selecting SIDECHAIN therefore
**falls back to CLOCK** for any block in which no input arrived, which is every
block today. Silently doing nothing when a user selects a source would be worse.

The grid and the MIDI queue are advanced on every block whichever source is
selected, so switching source never produces a burst of stale triggers or a clock
that has to catch up.

### Where the other four envelopes go

`MacroState` carries one `pulse` pointer. It is set to the **VOLUME** envelope —
the reference shape. The other four are reached through
`ModulationEngine::pulseEnvelope (PulseEngine::Destination)`.

**Nothing asks for them yet.** `NacarEngine`'s output stage applies both its
volume and its width destinations from `MacroState::pulse`, so today the WIDTH
duck is generated with its faster recovery and then consumed with the VOLUME
shape. That is a one-line change in the consumer, not here.

---

## The mod matrix — §54, §127

Eight routings: a source, a target parameter, a bipolar depth, an enable. The MOD
page persists them under `ids::MODMATRIX` / `ids::MODSLOT` with `modSource`
(display name), `modTarget` (the parameter's permanent string ID), `modDepth` and
`modEnabled`. `ModMatrix::rebuildFromTree` reads exactly what that page writes.

The target is resolved through `ParameterRegistry::fromString`, so a string ID
that no longer exists resolves to `PID::count` and the slot goes inert rather than
pointing at whatever now occupies that index.

### The thread handoff

**Two staging buffers, an atomic index, and a generation counter that the audio
thread validates across its own copy.**

```
writer (message thread)        reader (audio thread, once per block)
-----------------------        ------------------------------------
fill stage[1 - published]      g0 = generation            (acquire)
published = that index (rel)   if g0 == seen: nothing changed, done
generation += 1        (rel)   copy stage[published] into `live`
                               g1 = generation            (acquire)
                               if g1 == g0: accept, seen = g0
                               else:        discard, keep last block's set
```

The atomic index alone would be *almost* right, and the gap matters: two
publications inside one audio block — which a drag on a depth bar produces
easily — can land the second one in the buffer the audio thread is reading. The
generation counter closes that, because a torn copy is exactly a copy across
which the counter moved. The failure mode is one block of staleness during a
drag, never a torn read, and the reader never blocks, spins or retries.

### Sources

Available and real: **LFO 1, LFO 2, BREATH, PULSE, ORGANIC RANDOM, MEMORY,
MOTION, WORLD, ALTER**.

The macro sources are the knob positions, 0…1, not the derived influences: a user
routing MEMORY means the control they can see, not this engine's interpretation
of it. ORGANIC RANDOM is a second `BreathEngine` at fixed settings and a
different seed, not a smooth-random LFO — SMOOTH RANDOM is a shape the user can
already select on either LFO, and making the matrix source the same thing would
leave the instrument with fifteen sources and fourteen behaviours.

**Not available here, and not faked:**

* **ENV 1, ENV 2, VELOCITY, KEY TRACK** are per-voice. They only exist inside a
  sounding voice, and there is no meaningful global value for them — an average
  across voices would be wrong in a different way for every patch. Making them
  work means the *voice* consulting the matrix for its own targets, with its own
  envelope and velocity values, rather than this engine publishing a number.
  They read zero.
* **MOD WHEEL, AFTERTOUCH** are global, but nothing hands this engine the
  `MidiBuffer`. `setModWheel()` and `setAftertouch()` exist and are wired all the
  way to the matrix; **nothing calls them**, so both read zero.

### Units

`offsetFor(PID)` returns a **normalised** offset — the number to add to
`ParameterRegistry::normalised(pid)` before mapping back through the parameter's
own range. Depth is −1…1 and the global sources are −1…1 (Pulse and the macros
are 0…1), so one routing at full depth can move a parameter across its whole
range. The sum over routings is clamped to −1…1, because more than a full range
of offset means nothing. The query is O(1): `beginBlock` accumulates into a
per-parameter array and clears only the entries the previous block touched.

**Nothing consumes the matrix.** Every engine still reads its parameters directly
from the registry. `rebuildModMatrix()` is never called, so the live routing set
is empty and `modulationFor()` returns zero for everything.

---

## Parameter coverage

Every parameter in the MODULATION section of `ParameterList.h` is read and used:
`lfo1/2_shape, _rate, _sync, _div, _depth, _phase`; `breath_on, _amount, _speed,
_random, _shape`; `pulse_on, _source, _div, _depth, _attack, _release, _smooth`;
`pulse_volume, _filter, _space, _width, _memory`. None is accepted and ignored.

Two parameters outside this section are deliberately **not** read here:
`modwheel_depth` and `aftertouch_depth` are performance scalings that belong to
the voice's vibrato and expression path. The matrix slot already has its own
depth, and multiplying the two would make one knob silently scale the other.

---

## Realtime and determinism

After `prepare()`: no allocation, no locks, no file IO, no logging, no
`juce::String` anywhere reachable from `updateBlock()`. The registry is read once
per block into local structs. Nine buffers of `maxBlockSize` floats are allocated
in `prepare()` — two LFOs, Breath, Organic Random and five Pulse envelopes.

Every rate is expressed in Hz or beats and converted with the actual sample rate
and tempo, so a block-size change cannot change any modulator's speed: Breath's
control-tick counter and Pulse's grid both persist across block boundaries and
advance by elapsed samples.

Division by zero is not reachable at any parameter extreme. Tempo is clamped to
20…300 bpm before `beatsPerSample`, matching `fx::beatsToSeconds`. Division
indices are clamped into their tables and the beat values in those tables are
never zero. Attack and release times are floored at 0.1 ms and 1 ms before being
inverted. Sample rate is floored at 1 Hz. Breath's speed is floored at 0.001 Hz
and its jitter multiplier cannot reach zero.

---

## Known limitations — the honest list

- **Nobody has listened to any of this.** Everything above is an argument from
  construction, not a measurement and not a judgement about how it sounds.
- **Nothing consumes the mod matrix.** `rebuildModMatrix()` has no caller, so the
  routing set is always empty. The matrix edits and persists in the UI and
  resolves correctly here; it does not modulate anything.
- **The matrix is block-rate.** `offsetFor` returns one number per parameter per
  block. An LFO routed through it at 40 Hz will step at every buffer boundary. A
  per-sample matrix means the consumer indexing the source buffers itself, which
  is a different interface from the one specified.
- **Four sources read zero**: ENV 1, ENV 2, VELOCITY, KEY TRACK (per-voice — the
  voice must consult the matrix instead) and, for a different reason, MOD WHEEL
  and AFTERTOUCH (plumbing exists, nothing calls the setters).
- **The sidechain source has no input** and always falls back to CLOCK. The
  envelope follower has never processed a sample and its thresholds are chosen by
  reasoning, not by trying it on a kick.
- **The four non-volume Pulse envelopes have no consumer.** `NacarEngine` applies
  both volume and width from `MacroState::pulse`, so the width duck's faster
  recovery is generated and then discarded.
- **MIDI Pulse triggers are quantised to the block** because the caller uses
  `noteTriggered()` rather than `noteTriggeredAt()`. Up to 21 ms at 48 kHz.
- **An oversized block disables modulation for that block.** If a host hands over
  more samples than it promised in `prepare()`, the four pointers are set to null
  (which `MacroState`'s accessors already read as silence) rather than running off
  the end of the buffers. There is no way to grow them without allocating on the
  audio thread.
- **Both LFO depths default to zero**, so `lfo1` and `lfo2` are silent in a
  default session. That is the parameter list's choice, not this engine's, but it
  means "the LFOs do nothing" is the expected default and not a bug.
- **Breath freezes rather than resets when `breath_on` is false.** Turning it back
  on resumes the same trajectory. Deterministic, but it means the generator is not
  advancing while disabled, so an A/B of on-off-on is not the same as leaving it
  on.
- **Breath's non-repetition is an argument, not a measurement.** No
  autocorrelation test has been run on its output. The claim that it does not
  repeat rests on the irrational ratios and on the rate jitter being an integral
  of a random sequence.
- **The LFO cycle accumulator wraps** if it ever exceeds 10⁹ cycles (289 days at
  40 Hz), which would glitch a held random value once. Chosen over losing double
  precision.
- **The Pulse trigger list is capped at 32 per block.** Unreachable from the
  clock at any tempo and division, reachable from a dense MIDI chord only if more
  than 32 note-ons land in one block, in which case the oldest are dropped.
