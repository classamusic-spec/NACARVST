# The NÁCAR modulation layer

Everything downstream reads what this produces, so the output contract matters
more than any algorithm inside it.

```
ModulationEngine.h/.cpp   fills a MacroState's modulation fields each block
LFO.h/.cpp                seven shapes, free or locked to song position
BreathEngine.h/.cpp       organic non-repeating movement; also ORGANIC RANDOM
PulseEngine.h/.cpp        five differently shaped ducks from one trigger
ModMatrix.h/.cpp          eight routings, message thread to audio thread
SequencerEngine.h/.cpp    four lanes of sixteen steps, locked to the transport
```

`mod::Clock` and `mod::smoothStep` live at the top of `LFO.h` rather than in a
header of their own, because the clock is first needed by the first thing that
locks to tempo. Pulse, the synced LFOs and the sequencer all take one.

`SequencerEngine` is not part of `ModulationEngine`. It is owned directly by
`NacarEngine`, because a lane writes an *absolute position* for a parameter
while everything in `ModulationEngine` produces an *offset*, and the two only
compose where the overlay is written — see "The step sequencer" below.

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

**MIDI** — `noteTriggeredAt (offset)`. `NacarEngine` calls it once per note-on
with the note's own position inside the block, so the duck lands on the note
rather than at the top of the buffer. The no-argument `noteTriggered()` is still
there for a caller that does not know the offset; using it costs up to 21 ms at
48 kHz with a 1024-sample buffer, which on a kick is the difference between a
duck and a flam.

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
* **MOD WHEEL, AFTERTOUCH** are global and are now live. `NacarEngine::process`
  scans the block's `MidiBuffer` and calls `setModWheel()` on CC 1 and
  `setAftertouch()` on either channel pressure or polyphonic aftertouch,
  whichever the controller sends. Both latch at block rate like every other
  source.

### Units

`offsetFor(PID)` returns a **normalised** offset — the number to add to
`ParameterRegistry::normalised(pid)` before mapping back through the parameter's
own range. Depth is −1…1 and the global sources are −1…1 (Pulse and the macros
are 0…1), so one routing at full depth can move a parameter across its whole
range. The sum over routings is clamped to −1…1, because more than a full range
of offset means nothing. The query is O(1): `beginBlock` accumulates into a
per-parameter array and clears only the entries the previous block touched.

### How a routing becomes audible

No engine knows the matrix exists, and that is deliberate. Threading a matrix
query through eleven engines and roughly a hundred and forty parameter reads
would have meant editing every one of them, and every engine written afterwards
would have had to remember to do the same.

Instead `ParameterRegistry` carries a **modulation overlay**: one atomic per
parameter, holding either the value the matrix wants or a sentinel meaning "not
modulated". `raw()` returns the overlay when there is one and the user's value
otherwise, so every existing `p.raw (PID::…)` in the instrument picks modulation
up for free. `userValue()` is the unmodulated read, and the interface uses that
one — a knob that jitters because an LFO is running is not showing the user
their own setting, and typed value entry into a moving field is impossible.

`NacarEngine::process` is the only writer. Once per block, after
`ModulationEngine::updateBlock` and **before the synth renders**, it walks the
live routings, writes an override for each distinct target, and — the part that
is easy to get wrong — **clears the overrides the matrix has stopped
targeting**. A stale override is a parameter frozen at whatever the matrix last
pushed it to: a knob that has quietly stopped working, with nothing on screen to
explain why.

Two consequences worth stating:

* The macro resolver runs **twice** per block, once before the overlay is
  applied and once after. The matrix's macro *sources* are the knob positions —
  a user routing MEMORY means the control they can see — so those are latched
  from the unmodulated values on the first pass. A macro is also a legal
  *target*, and without the second pass it would be the one dead entry in the
  target menu. There is no loop, because the two passes read different things.
* Because the overlay is written before `synth.process`, a routing reaches the
  oscillators in the same block it was computed for rather than the next one.

`Tests/Main.cpp` has a `Mod matrix` suite that asserts an unrouted parameter
reads exactly what the user set, that a routed one moves and keeps moving, that
modulation never pushes a parameter outside its own range, that removing a
routing gives the parameter back, that a target string that no longer exists is
inert rather than wrong, that the four per-voice sources really do read zero,
and — separately from all of those — that a routing changes the rendered audio,
which an overlay that engines never read would fail.

---

## The step sequencer — §129

Four lanes of sixteen steps. Each lane has its own length (1…16), its own
enable and its own target parameter; the whole sequencer shares one tempo
division. The SEQ page persists all of it under `ids::SEQUENCER` /
`ids::SEQLANE`, with `laneTarget` (the parameter's permanent string ID),
`laneEnabled`, `laneLength`, `laneValues` (sixteen comma-separated floats) and
`laneGates` (a sixteen-character mask), plus `seqDivision` on the parent.
`SequencerEngine::rebuildFromTree` reads exactly what that page writes.

The identifiers and the division table live in `Source/Plugin/StateManager.h`
(`ids::` and `seq::divisions`), not in the page, because `Source/Audio` must not
include `Source/UI` and two copies of a table that pairs a printed name with a
beat value is a pattern that plays at a tempo the interface does not show.

### The handoff

**Exactly `ModMatrix`'s**, deliberately — two staging buffers, an atomic index
and a generation counter the audio thread validates across its own copy. There
is no second pattern in this instrument and there must not be one. Dragging a
value across a lane publishes many times per block, which is the case the index
alone gets wrong and the counter fixes.

### Position, not accumulation

While the transport runs a lane's step is computed from `ppqPosition` — the
position of the block's *first* sample — with nothing accumulated:

```
step = floor(ppq / beatsPerStep)  mod  laneLength
```

so pressing play twice over the same bar is the same performance twice, and a
loop, a scrub or a jump needs no resynchronisation state. `floor`, not
truncation, so a negative ppq during a count-in walks backwards through the
pattern instead of sticking on step 0. When the transport is stopped an internal
beat counter runs at the host tempo, kept level with the song position while the
transport runs, so stopping continues from where the song was — the same
convention Pulse and the synced LFOs already use.

Because the step is a pure function of position, per-lane lengths actually
drift: a three-step lane against a four-step lane disagrees on nine of the
eleven steps between realignments, and both return to step 0 at twelve.

### A gate of 0 holds; it does not zero

A silent step means "nothing new happens here", so the lane keeps outputting the
most recent gated step's value. Zeroing instead would slam a sequenced cutoff
shut on every rest.

The hold is resolved by **searching backwards through the pattern** — the most
recent gated step at or before the current one, wrapping inside the lane's own
length — rather than by remembering what was played. That is what keeps the
sequencer a pure function of position: a lane entered halfway through a bar
holds exactly what it would have held had it been running all along. A lane with
no gates at all holds nothing and writes nothing, which is the default pattern
the SEQ page creates.

### The units, and how a lane composes with a routing

A step value is 0…1 and it is **absolute**: it names a position across the
target's whole range, not an offset from the knob. The step well draws a bar
whose height is the value, and there is no per-lane depth control on the page —
inventing one here would be adding a control to a locked reference.

`NacarEngine` turns that into the overlay's units and adds the matrix's offset
to it:

```
offset(target) = matrixOffset(target)                         // ModMatrix
               + laneValue - normalisedUserValue(target)      // SequencerEngine
```

so **the lane sets the parameter's position and the matrix moves around it**. An
LFO routed to a sequenced cutoff wobbles each step rather than being silently
discarded; a lane on an unrouted parameter lands on exactly its step value.
`ParameterRegistry::setModulation` clamps the sum into the parameter's own
range, so neither can push it out.

Two lanes naming the same parameter cannot both set a position, so the
**lowest-numbered** one owns it and the other is ignored for that block. First
writer rather than last, so adding a lane 4 later cannot quietly take lane 1's
parameter away.

`applyModulation` clears an override the moment nothing targets it any more,
exactly as it does for the matrix: a stale override is a knob frozen at the last
step the sequencer played.

### Where it runs

`NacarEngine::process` calls `sequencer.beginBlock` before
`modulation.updateBlock`, and both before `applyModulation` and before the synth
renders, so a sequenced parameter and a routing to it move in the same block
rather than one apart.

### Cost and limits

`beginBlock` is O(lanes × steps) — sixty-four compares — and allocates nothing,
locks nothing and builds no `juce::String`. Everything the audio thread reads is
a plain value resolved on the message thread.

The overlay carries one value per parameter per block, so the sequencer is
block-rate like the matrix. A step boundary inside a block takes effect at the
top of the next block: late by at most one buffer, never early, never skipped.
A step shorter than a buffer would be under-sampled — 1/32 at 300 BPM is 25 ms
against 46 ms for a 2048-sample buffer at 44.1 kHz — and because the step comes
from position rather than an accumulator the lane stays locked to the song
rather than falling progressively behind.

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
- **The matrix is block-rate.** `offsetFor` returns one number per parameter per
  block. An LFO routed through it at 40 Hz will step at every buffer boundary. A
  per-sample matrix means the consumer indexing the source buffers itself, which
  is a different interface from the one specified.
- **Four sources read zero**: ENV 1, ENV 2, VELOCITY and KEY TRACK. They are
  per-voice quantities and making them work means the *voice* consulting the
  matrix for its own targets, not this engine publishing a global average. They
  are inert on purpose, and a test asserts they stay that way so that anybody who
  later wires a global stand-in has to say so out loud.
- **A modulated parameter is still modulated one block at a time.** The overlay
  holds a single value for the whole block, so a routing that sweeps a parameter
  fast enough will step at buffer boundaries — the block-rate limitation above,
  seen from the consumer's end. Per-sample destinations that matter (the synth's
  own LFO paths, Pulse's five envelopes) bypass the matrix entirely and read the
  per-sample buffers directly.
- **Two routings onto the same target sum and then clamp.** That is the
  documented behaviour, but it means the second routing can appear to do nothing
  once the first has already pushed the parameter to a rail.
- **The sidechain source has no input** and always falls back to CLOCK. The
  envelope follower has never processed a sample and its thresholds are chosen by
  reasoning, not by trying it on a kick.
- **Three of the five Pulse envelopes have no consumer.** `NacarEngine`'s output
  stage takes VOLUME from `MacroState::pulse` and WIDTH from
  `pulseEnvelope (Destination::width)`, so those two have their own shapes as
  intended. FILTER, SPACE and MEMORY are generated and then discarded: the
  engines that own those behaviours would each have to read them, and none does
  yet.
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
- **The sequencer is block-rate and has no per-lane depth.** Both are stated
  above rather than hidden: a lane is absolute across its target's whole range,
  and a step boundary inside a block lands at the top of the next one.
- **Nothing creates the SEQUENCER branch until the SEQ page is opened.** A
  session that has never been to that page publishes four empty lanes, which do
  nothing — correct, but it means the sequencer is inert in a brand-new session
  until the page has been visited once.
- **The Pulse trigger list is capped at 32 per block.** Unreachable from the
  clock at any tempo and division, reachable from a dense MIDI chord only if more
  than 32 note-ons land in one block, in which case the oldest are dropped.
