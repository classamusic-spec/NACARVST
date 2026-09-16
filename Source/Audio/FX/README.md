# The NÁCAR FX chain

Six engines, in whatever order the user has put them.

```
RetroEngine.h/.cpp    §85  the medium and the playback machine
CrushEngine.h/.cpp    §86  bit and rate reduction
FilterFX.h/.cpp       §87  the chain filter
RewindEngine.h/.cpp   §88  a transport gesture over recent history
GrainFX.h/.cpp        §89  a granular reconstruction of the chain signal
SpaceEngine.h/.cpp    §90  the production reverb
```

§91 makes Rewind, Grain and Space the signature path. §38, §40 and §43 constrain
all six at once and are dealt with once, near the end, rather than six times over.

Everything they are built from is in `../DspCommon.h` — the Hermite-interpolated
`DelayLine`, the `Allpass`, the complementary `TwoBand` / `ThreeBand` TPT splits,
the level-neutral `Tilt`, the stereo `HistoryBuffer`, the equal-power
`dryWetGains`, `guard()` — which borrows the synth core's primitives by name
rather than re-writing them, so the instrument has one set and not two.

---

## The slots, and the thread handoff

`FxSlot` is `retro, crush, filter, rewind, grain, space`. Display order is DSP
order: the FX chain view's card order **is** the signal path.

Order and per-slot bypass live in the session tree (`FXCHAIN`, as `fxOrder` and
`fxBypass`, comma-separated slot names). The audio thread must never read a
`ValueTree`, so `NacarProcessor::publishFxOrder()` resolves both on the message
thread, `FxOrder::pack()` folds them into 32 bits — six three-bit slot indices, a
three-bit count, a six-bit bypass mask, 27 bits used — and one relaxed atomic
store publishes them. The audio thread unpacks at the top of each block.

`fromState` survives whatever is in the tree: a repeated name is taken once (one
engine cannot be in two places in a chain), an unknown name dropped, an empty
order falls back to the default. `unpack` re-clamps every field, so a corrupt
word gives the default order rather than an out-of-range index.

The chain sits inside a fixed outer order the user cannot change:

```
SYNTH -> MEMORY -> [the six slots] -> SHADOW -> AURA -> PATINA -> WEIGHT -> OUTPUT
```

`NacarEngine.h` argues each placement. The output stage applies Pulse's volume
duck and, through a 140 Hz / 2.6 kHz split, its width duck — the low band is
collapsed to mono there and never scaled.

## Bypass, and why the chain does not gate

Each engine owns its own bypass and each one is *exact*: once its mix smoother has
reached zero the buffer is not touched at all, so the output is the input sample
for sample rather than merely close to it. The chain never refuses to call an
engine, and `NacarEngine::runFxSlot` says why. Retro and Crush read their **dry**
tap from a delay line, so a line that stops being written replays stale audio on
the way back. Rewind and Grain read a history buffer, and a module that only
starts remembering when it is switched on has nothing to play back for its first
seconds. Space flushes its tail **on the transition**, which it can only do if it
sees the transition.

Two controls reach the same thing — a power ring bound to `*_on` and a `-` glyph
that writes `fxBypass` — and the engines only know about the first. So
`applyFxBypass()` translates the second into the first before the chain runs: for
every slot the order does not reach, or that the mask mutes, it forces that slot's
`*_on` flag to its minimum through the registry's modulation overlay, and for every
live slot it clears the override. One code path, every engine sees a power-off
exactly as it always has, and `updateLatency` — which runs immediately afterwards
off the same flags — stops reporting a delay for a module that is muted.

---

## RETRO — §85, §95

§95 draws the line: Memory is historical identity, Patina is surface, Retro is *the
medium and the transport the sound is playing back through*.

**ERA is not an amount.** It selects which machine, morphing continuously between
four profiles — ACETATE 1948, VALVE TAPE 1958, CASSETTE 1979, SAMPLER 1987 — of
twenty-one fields each, from bandwidth and saturation curve through wow and
flutter rates to noise colour, mono collapse, azimuth and converter word length.
Adjacent rows interpolate field by field, so halfway between CASSETTE and SAMPLER
is a machine halfway between them, not a crossfade of two outputs. The converter's
rate and word length are the exception — a machine either has a grid or it has not
— so they come from the SAMPLER row, on a separate ramp starting at ERA 0.70.

```
write the transport delay line
dry tap at the nominal delay (4 ms)         -> time-aligned dry path
wet tap at nominal + wow + flutter + scrape
low cut -> saturation -> dynamic low pass -> resampling grid
three-band split (130 Hz / 2.5 kHz)
  low   common-mode dropout gain only
  mid   per-channel dropout, partial mono collapse
  high  per-channel dropout, mono collapse, azimuth trim
+ medium noise, + common-mode rumble -> Tilt (TONE) -> DC blocker
equal-power dry/wet
```

Wow, flutter and scrape are three mechanisms: wow is two incommensurate sines
(0.65–1.6 Hz by era) whose rate itself wanders through a 1.5 s one-pole, flutter
is one faster sine (7–14 Hz), scrape is white noise through two 120 Hz one-poles.
All three are specified as a peak *fractional pitch deviation* and converted to a
delay excursion with the real sample rate, so DRIFT means the same musical amount
at 44.1 and 96 kHz. The transport is shared by both channels — one capstan.

The saturator is `lerp(soft, hard, digitalness)` divided by the drive, so its
small-signal gain is 1 and AGE cannot make the module louder (§148). The bandwidth
filter's corner falls with a 3 ms level follower, which is dynamic HF loss on
sustained material and transient rounding on an attack out of one mechanism.

WEAR is a memoryless scheduler at a 32-sample control rate: `wear^1.7 × 4.5` events
per second, each 8–200 ms, a little over half on one channel only, reaching their gain
and HF trim through 6 ms one-poles so an event cannot click. Under it a 0.4 s random
walk per channel pulls one channel's top end against the other — azimuth, which only
ever *trims*, because a misaligned head gap cannot add high end.

The noise takes the era's colour and is **not ducked when the music plays**: ducking a
noise floor is a gate and a medium has no gate. What is modelled instead is tape's
modulation noise, rising with recorded level. Crackle is generated impulses through the
same band pass — no vinyl sample, no static loop — and rumble is generated once and
added identically to both channels.

**Macros.** `age` → +0.25 AGE and +0.30 WEAR; `grit` → +0.25 NOISE and 0.30 of the
saturation drive term; `movement` → +0.30 DRIFT; `alterAmount` → up to +0.34 ERA,
one whole machine; `breath` → ±18 % on the wow *depth*. `scale`, `distance`,
`wetBias`, `widthScale` and every Pulse destination are ignored.

**Bypass.** `retro_on` false sets the mix *target* to zero rather than jumping
there, so the off edge glides over the same ~25 ms the on edge does — MIX defaults
to 1.0, and an instant bypass would replace the whole output in one sample. Once the
smoother arrives the engine early-outs: transport lines still written, buffer
untouched.

**Latency.** 4 ms, 192 samples at 48 kHz, returned unconditionally by
`getLatencySamples()`. The wet tap must wander either side of a centre tap and the
dry tap is read from the same line at exactly that centre, so the mix cannot comb.
`updateLatency` counts it only while the flag is true, which now accounts for the
mask as well.

**Gain staging.** Unit small-signal gain through the saturator; azimuth and dropouts
only attenuate; each era carries an `outputTrim` (1.00–1.10) that level-matches the
four machines.

---

## CRUSH — §86

```
dry line (9 samples, latency compensation)
2x up -> blended tanh drive -> 2x down
sample and hold on the reduced grid, with jitter
    the held value is quantised at the instant it is taken
makeup -> Tilt (TONE) -> DC blocker (wet only) -> equal-power dry/wet
```

**The quantiser sits inside the hold** because a converter quantises when it
samples. That also makes the first-order error feedback correct: it shapes the
error between consecutive *taken* samples, which is the rate at which the error is
generated. Quantising at full rate on a held signal would feed the same error back
repeatedly and turn a noise shaper into a slow integrator.

BITS is mid-tread — it has a zero level, so silence stays silence — with TPDF
dither and error feedback bounded to one step. Dither fades out below 8 bits and is
off below 3, where one LSB of it would be louder than the music. Such a quantiser
settles up to half a step from zero, which at one or two bits is an enormous
offset, so the wet path ends in its own DC blocker; the dry path is untouched, so
exact bypass stays exact.

RATE is a sample and hold, **not oversampled and deliberately so**: a sample-and-hold
is an undersampler and its fold-back *is* the effect. JITTER draws a new hold period
at every take, bounded to `[1, 4×base]`, so the clock wobbles and the images smear
instead of landing on fixed frequencies.

DRIVE is the one nonlinear stage here not supposed to alias, so it runs at 2× through
the halfband, blended rather than switched (`lerp(x, tanh(gx)/g, d)`) because tanh is
not the identity near full scale and a shaper that engages at a threshold steps as
the control leaves zero. Makeup is exactly the reciprocal of the blend's small-signal
gain. CRUSH is the master: bits interpolate towards 24, rate towards the host rate
**in the log domain**, jitter and drive towards zero, so CRUSH 0 is transparent.

**Macros.** `grit` → +0.25 CRUSH; `age` → +0.10 CRUSH, smaller because a converter
does not wear out. Everything else is ignored: a converter does not drift.

**Bypass.** `crush_on` false ramps the mix to zero over the same time the on edge
takes; once it arrives the engine early-outs with the dry lines still written and the
buffer untouched.

**Latency.** 9 samples — `(19 - 1) / 2` — at the base rate, whatever the sample rate.
The upsampler delays by 9 high-rate samples and the downsampler by another 9; 18
high-rate is 9 low-rate, an integer, so the dry path is delayed by the same integer
and needs no fractional interpolation.

**Gain staging.** Drive is compensated exactly at small signal; because tanh
compresses above that, DRIVE at maximum costs 3–8 dB on loud material. A driven stage
that cannot get louder has to get quieter — the §148 trade — and DRIVE is therefore
not a level control.

---

## FILTER — §87

It contains no filter of its own: four `synth::SynthFilter` instances, two per
channel, the same family the synth voice uses with the guards already in them. A
seventh filter would have given the instrument two filter characters and the
specification asks for one.

The four standard responses use the **HAZE** state variable rather than the MASS
ladder, for two mechanical reasons. All four fall out of the same two integrators,
so morphing between them is continuous by construction. And the ladder always has a
saturator in its feedback path even at drive zero — which the synth answers with 2×
oversampling, an answer unavailable here, because a chain filter with a dry/wet
control cannot be oversampled without putting latency into the dry path or combing
the mix.

```
blended input drive -> filter A (selected) -> filter B (next along, if MORPH > 0)
equal-power crossfade A/B -> equal-power dry/wet
```

MORPH walks LP, HP, BP, NOTCH, COMB, FORMANT and wraps. Both filters see the same
input and identical coefficients, so crossfading their outputs is the same operation
as crossfading two taps of one filter. It is equal power because adjacent responses
are partially complementary and a linear crossfade of a complementary pair loses 6 dB
in the middle.

MOTION is self-modulation, explicitly not an LFO. Four sources sum and none dominates:
Breath (0.42), two incommensurate slow sines at 0.063 and 0.101 Hz (0.33), a random walk
with a new target every 64 samples through a 0.9 s one-pole (0.25), and — separately — a
30 ms follower on the previous output that opens the filter as the material gets louder.
That last path is feedback and is bounded on purpose: the follower is clamped to [0, 1]
and scaled to at most 0.6 octave, the cutoff is assembled and clamped in the log domain
to [20 Hz, 20 kHz] before it becomes a coefficient, and `SynthFilter` clamps again to
0.45 of Nyquist. MOTION also drives the vowel position, so a FORMANT patch travels
through vowels instead of sitting on one. Drive is applied here rather than through
`SynthFilter::setDrive`, whose internal drive switches on at a threshold and therefore
steps as the control leaves zero.

**Macros.** `movement` → +0.30 MOTION; `breath` → one of the four motion sources;
`pulseToFilter × pulse` → up to −1.8 octaves of cutoff per pulse, §83's "a kick makes
it darker". `age`, `grit`, `scale`, `distance`, `wetBias`, `widthScale` and
`alterAmount` are ignored.

**Bypass.** `fxfilter_on` false ramps the mix to zero — a resonant filter's wet signal
can be far from its dry one — and once it arrives the engine returns with the buffer
untouched. There is nothing to keep warm: no latency, no delayed dry path.

**Latency and gain staging.** None, beyond the drive compensation and the two
equal-power crossfades.

---

## REWIND — §88, §91

A gesture, not a continuous effect: triggered, runs for a window, ends. One stereo
`HistoryBuffer` (8 s requested, 10.9 s after the power-of-two round-up at 48 kHz,
4.2 MB) is written with the input every sample, unconditionally.

The engine is one read tap described by two numbers, tied exactly: the write head
advances one sample per sample, so `delay += (1 - rate)`. Rate 1 holds the delay
still, 0 freezes the position, negative runs backwards. The four modes are four
trajectories for `rate`:

| mode | trajectory |
|---|---|
| REVERSE | `-SPEED · (1 + k(2u - 1))`, k from CURVE; mean exactly 1 for any k, so the window covers the same material whatever the shape |
| STOP | `(1 - u)^p`, reaching exactly zero at the end — a halt, not a crawl |
| DIVE | `2^(-3·u^p)` — linear in octaves, so the pitch falls rather than the rate; ends three octaves down and still moving |
| RETURN | jumps back by the window, plays forward at SPEED, then over the last CURVE-sized fraction sets `rate = 1 + remainingDelay / remainingSamples` to land exactly on realtime |

**Three ways this could click, three fixes.** A discontinuous read position — only
RETURN's jumps and a re-trigger create one — goes through `jumpTo()`, which keeps the
retiring tap *moving* at its old rate and equal-power crossfades over 6 ms; a frozen
old tap is a held sample, and fading out of one is itself a discontinuity in the
first derivative. A discontinuous rate goes through a slew limiter needing about 3 ms
to cross the whole rate range. The module arriving or leaving is an envelope: 6 ms up,
`8 ms + TAIL × 1.2 s` down, multiplied into the mix before `dryWetGains`, so at env 0
the dry gain is exactly 1 and the wet exactly 0. REVERSE, STOP and DIVE start at delay
1 with rate 1 — the present at realtime, which *is* the input — so they need no
crossfade at the start.

`ParameterList.h` has no trigger parameter, so `rewind_on` is a gate that re-arms: its
rising edge fires immediately; with SYNC it re-fires on the `rewind_div` grid derived
per sample from `ppqPosition`; without SYNC it free-runs every `rewind_length` seconds;
and a rising edge of Pulse through 0.5 fires it too (§88). A lockout of a quarter of
the window stops a fast grid machine-gunning it; a hand press is exempt.

**Macros.** `pulse` triggers; `age` → +0.25 TAIL; `movement` → ±12 % on the window
length drawn at each trigger, so a grid-locked Rewind is not metronomically identical;
`alterAmount` → ±0.25 on CURVE per trigger; `breath` → ±3 % on the free-run period.
`scale`, `distance`, `wetBias`, `widthScale` and `grit` are unused.

**Bypass.** Never early-returns, so the history is always written. When no gesture is
active and the envelope has reached zero, each sample snaps the tap to the present and
continues, leaving the buffer untouched. `rewind_on` going false stops new triggers but
lets a running gesture finish and tail out; MIX zero at both ends abandons it instead.

**Latency.** None. **Gain staging.** None: the wet path is a read of material already
at the chain's level and nothing here sums, so the only gain applied anywhere is
`dryWetGains`, and the one place two signals coexist — the tail overlap — is bounded by
the equal-power pair.

---

## GRAIN — §89, §91, with the pitch behaviour of §108 and §119

A pool of 96 grains, each completely described at the instant it is spawned — start
position, signed increment, length, window, left and right gain — and never revisited.
That is what makes any parameter safe to change at any time: the change reaches the
*next* grain and the ones in flight finish the way they started. An exhausted pool
**drops** the new grain rather than stealing a sounding one: a dropped grain is silence
that was never scheduled, a stolen one is a window cut off mid-envelope. Its own 4 s
history (5.46 s after rounding at 48 kHz) is written every sample, and a grain's read
span is computed at spawn and clamped so it fits inside the buffer without crossing the
write head; a span that will not fit is dropped.

**Windows** are five 1024-point tables plus a guard point, built in `prepare()` and read
with linear interpolation: HANN, TUKEY (25 % taper), GAUSS (σ = 0.16, shifted so the ends
are exactly zero), EXPO, PERCUSSIVE. Both endpoints of every table are forced to zero —
at 80 grains a second a non-zero endpoint is a buzz, not a click. Each table's mean and
RMS are measured at the same time, for the gain law.

**Pitch is a weighted distribution, never uniform** (§119). `buildPitchTable` writes the
specification's ordering out literally across the 25 semitones from −12 to +12: unison
100, octaves 30/26, fifths 13/9, other chord tones 6/4, other scale tones 1.6/1.2,
non-scale 0.5 — downward intervals slightly below their upward twins, because
transposing granular material down thickens the low mids faster than transposing it up
thins them. Three things then multiply those weights: `grain_pitch_mode` decides which
candidates are allowed at all; `harmony_mode` caps the tier (§108's SAFE / COLOR /
FREE), and a candidate above the cap is multiplied by `alterAmount²` rather than by
zero, so Alter opens the door as a fade and not a switch; `grain_scatter` multiplies
every non-unison weight by `scatter^1.5`, collapsing the distribution onto unison as
SCATTER closes. FREE mode bypasses the table and uses `grain_pitch` plus ±50 cents.

`root_note` and `scale_type` both have AUTO and there is no analysis engine yet, so AUTO
resolves to C and to MINOR here — minor because a minor third over major material is a
colour and a major third over minor material is a mistake. `root_note` is then read and
deliberately **not used**: a grain's transposition is relative to whatever the material
already is, so the tonic cannot change which intervals are in key.

**Gain staging** is this engine's §148 section: density must mean density, not volume.
Expected overlap is `DENSITY × SIZE`, and the normalisation is the
coherence-interpolated law the synth's unison normalisation uses:

```
gain = 1 / ( overlap^(0.5 + 0.5c) · windowRms^(1-c) · windowMean^c )
c    = P(0 semitones) · (1 - jitter) · directionCoherence
```

`c` is not guessed: `P(0 semitones)` is the unison share of the distribution just built,
so a ROOT-mode cloud at low jitter is treated as coherent and a CHROMATIC one is not.
The gain is clamped to 2×, which binds for PERCUSSIVE and EXPO at low density —
loudness-matching a sparse stream of an 89 %-silent window would ask for nine times gain
and produce peaks nine times the source's. §148 cuts both ways.

**FREEZE** copies 2.5 s of history into a separate store at 32 store samples per audio
sample — bounded work, no allocation — completing in 78 ms. Grains keep coming from the
live history until it is full, and a grain never changes source mid-flight, so neither
edge of FREEZE can click. While frozen, POSITION scrubs the held slice.

**FEEDBACK** is bounded four ways and all four are needed: the parameter maxes at 0.95
and the engine applies a further 0.95 (loop gain 0.9025); a 6.5 kHz one-pole inside the
loop, because upward transposition moves energy up and without a spectral leak the loop
converges on a whistle at Nyquist; a DC blocker inside the loop, because the asymmetric
windows rectify slightly; and a peak-following limiter (5 ms / 400 ms) holding the
fed-back signal at or below 1.0 before a final tanh.

**Macros.** `movement` → +0.35 JITTER and scales the Breath position drift; `scale` →
+0.30 SPREAD; `distance` → +0.15 SPREAD; `widthScale` multiplies every grain's pan;
`alterAmount` opens the harmony tier and gives each grain a `0.6 × alter` chance of the
alternate window (PERCUSSIVE when the patch asks for anything else, HANN when it asks
for PERCUSSIVE); `breath` drifts the read position by up to ±8 % of the history, scaled
by movement. `age`, `grit`, `wetBias` and the Pulse destinations are unused.

**Bypass.** `grainfx_on` false ramps the mix to zero across a block. Once it arrives
every grain is deactivated, the feedback state zeroed, the history still written and the
buffer untouched; the stored mix is forced to zero so re-enabling ramps up from silence
rather than stepping to full wet.

**Latency.** None.

---

## SPACE — §90, §92

A feedback delay network. The brief is blunt: it has to be good enough that somebody
reaches for it instead of the reverb they already own, which rules out the usual failure
of a built-in reverb — a "distance" control that is a wet-level control wearing a
different name.

```
in -> air absorption ------------------------------------------> direct
  \-> 72 Hz high pass -> PRE-DELAY -+-> 8 early taps ----------> early
                                    +-> 4 series allpasses -> 8-line FDN
                                        (Hadamard mix, damping and subsonic
                                         cut inside the loop)     -> late
early·earlyGain + late·lateGain -> Tilt -> three-band width stage -> wet
out = direct·dryGain + wet·wetGain
```

The eight delay lengths are the primes 1129…2903 as milliseconds at 48 kHz
(23.52–60.48 ms). The primality is not the point; the mutually incommensurate ratios are
— delays in simple ratios share modes, and shared modes are what a metallic tail is.
Scaling all eight by the same factor preserves the ratios at every SIZE and sample rate.
The mixing matrix is an orthonormal Walsh-Hadamard scaled by 1/√8, so it can neither add
nor remove energy and *all* the loop gain sits in the explicit per-line gain, which is
what makes the stability bound a bound.

**Damping is inside the feedback path**, before the matrix, so it applies once per
circulation: that is the difference between a reverb that gets darker as it decays — what
a real room does — and one that is simply dull. Per-line cutoffs are spread ±12 % so the
eight lines do not darken in lock step. The subsonic cut in the same place is not tone
shaping; it stops a high-feedback network accumulating DC and sub-20 Hz energy.

**DISTANCE is six cues, in order of how much they matter** (§92): the early reflections
rise against the direct sound (0.42 → 1.0 of the character's early level); the direct
sound loses high frequencies (a 3.8 kHz one-pole blended in up to 85 %); the pre-delay
**shortens** by up to 75 %, which is the cue usually implemented backwards — a distant
source is nearly as far from you as its reflections are, so a long pre-delay reads as
*near*; the image narrows by up to 28 %; the tail damping darkens by up to 35 %; and only
then does the wet/dry ratio move, by 14 % of the remaining headroom. The two cues that
touch the direct path are scaled by how present the wet is, so they vanish as MIX does.

The five characters are five configurations of the network, not one reverb with an EQ:
ROOM uses only two of the four diffusers so its early field stays discrete, DARK damps at
2.4 kHz inside the loop, INFINITE pins the feedback at 0.99997 and trims the input to 0.22
so the network fills rather than floods. RT60 is clamped to [0.08, 28] s and the per-line
gain to 0.9992, and a soft ceiling inside the loop — transparent below 1.5, asymptotic to
3.0 — turns INFINITE from something that accumulates without bound into something that
fills and holds.

**Macros.** `scale` → +0.30 SIZE; `distance` → +0.35 DISTANCE — and because DISTANCE is
the cue set above rather than a wet control, World is not mapped to wet level, which §73
forbids; `wetBias` → +0.25 on MIX, an offset and not a replacement; `movement` → +0.10 ms
of delay modulation depth; `widthScale` multiplies the tail width; `pulseToSpace × pulse`
ducks the wet by up to 85 %, §83's "drier" seen from here; `breath` drifts every delay
length by ±0.4 %. `age`, `grit` and `alterAmount` are unused.

**Bypass.** `space_on` false, or MIX at or below 1e-5: `flushNetwork()` on the transition
— one memset, never per block — then return with the buffer untouched. The gate is the
parameter *alone*, not the parameter plus the macro: a patch that asks for no reverb gets
none however far World is turned up. World may colour an effect; it may not summon one.
Unlike the other five, this edge is not ramped — see the limitations.

**Latency.** None; the pre-delay is on the wet path only.

**Gain staging.** The early field is normalised by the energy sum of its tap gains, so its
level does not depend on how many taps the pattern has. The injection gain (0.45, or 0.22
for INFINITE) and the 0.5 output tap normalisation are **reasoned, not calibrated**.

---

## The low-end rule — §38, §40, §43

Width is never bought at the cost of the low end. What each engine actually does:

* **Retro** — the transport modulation is common to both channels; a dropout applies only
  its *common* part (the smaller of the two channel gains) to the low band, so everything
  the channels differ by stays above 130 Hz; mono collapse and azimuth touch mid and high
  only; the hiss is high-passed twice above the low crossover and the rumble is generated
  once and added identically to both channels.
* **Crush** — one clock and one dither generator serve both channels, and both run identical
  coefficients, so identical input gives identical output and nothing can decorrelate at any
  frequency. Independent per-channel clocks would have been the obvious mistake: wider, and
  fatal to mono compatibility.
* **Filter** — both channels see identical coefficients every sample. A high pass or a notch
  removes low end from both equally, which is the control doing its job.
* **Rewind** — L and R are read at identical positions, so correlation is preserved exactly.
  The wet path additionally gets two cascaded 22 Hz one-poles, because DIVE ends three
  octaves down and would otherwise put a 60 Hz note at 7.5 Hz.
* **Grain** — the only engine that genuinely decorrelates, because SPREAD pans individual
  grains. The summed grain output is split at 120 Hz / 3 kHz and the low band collapsed to
  mono before the bands are summed back, so SPREAD and `widthScale` reach mid and high only.
* **Space** — the feed into the wet path is high-passed at 72 Hz before it reaches anything
  that remembers, so the bass is not in the tail at all; the per-line loop cut (60–120 Hz by
  character) is the second guard; the wet output is split at 150 Hz / 2.6 kHz with the low
  band mono and the width on mid and high only. The two output taps take disjoint sets of
  lines rather than the same lines with opposite signs, because anti-correlated is what
  vanishes in mono.
* **The chain's output stage** splits at 140 Hz / 2.6 kHz and collapses the low band to mono
  before applying `widthScale` and Pulse's width duck.

`Tests/Main.cpp` has a chain test that renders 150 blocks with Retro, Filter, Space, Aura,
Shadow and Patina on, World at 1.0 and Memory at 0.8, low-passes both channels at 150 Hz and
asserts the correlation stays above 0.85. **Crush, Rewind and Grain are not enabled in it**,
so the one engine whose stereo behaviour actually needs its band split is the one the test
does not cover. That test was not run while this document was written, and no measurement of
any kind was taken here.

## Realtime — §146

After `prepare()`, nothing in these six `process()` methods allocates, locks, touches the
filesystem, logs, or constructs a `juce::String`. Every buffer — delay lines, histories, the
freeze store, the window tables, the grain pool — is sized in `prepare()` and never grows.
Every random quantity comes from a deterministic `fx::Rng` seeded in `prepare()` and
re-seeded in `reset()`, so an offline render reproduces a realtime pass sample for sample.

Every rate is expressed in Hz, seconds or beats and converted with the actual sample rate, so
a block-size change cannot change anything's speed: Retro's control counter, Grain's scheduler,
Rewind's grid phase and lockout all advance by elapsed samples across block boundaries. Each
engine clamps its block length to `jmin(macros.numSamples, buffer.getNumSamples())`, and Grain
clamps its freeze copy budget against `maxBlockSize`, so an oversized block cannot run off the
end of anything. Every sample that leaves any of the six goes through `fx::guard()`, which
replaces a non-finite value with silence and bounds the rest to ±8, and Space checks its wet
sum every sample and flushes the whole network if it has blown up.

## Parameter coverage

Every parameter in the RETRO, CRUSH, FILTER FX, REWIND, GRAIN and SPACE groups of
`ParameterList.h` is read and used. None is accepted and ignored. Three parameters from
outside those groups are read by Grain: `scale_type` and `harmony_mode`, both used, and
`root_note`, read and deliberately unused for the reason given above. Each engine reads its
own `*_on` flag as well as the chain resolving it, so the engine is still correct when a test
drives it directly.

---

## Known limitations — the honest list

* **Nobody has listened to any of this.** Everything above is a claim about what the code
  does. No build has been auditioned, no engine A/B'd against a reference. What has been
  measured is composition rather than character: see the test list near the end of this file.

### Fixed since this file was first written

An earlier draft listed four defects that are no longer present, and they are recorded here
rather than deleted because each cost real debugging and each could come back.

* **Retro's and Crush's bypass edges jumped in TIME**, by 4 ms and 9 samples. The bypassed
  path is the undelayed input; the active path's own dry tap is that input delayed by the
  alignment offset, so no amount of ramping the mix could close a gap that was not in gain.
  Both now crossfade against the live input with a dedicated `engageSm`, which for 4 ms is a
  tape splice and sounds like one. `Tests/Main.cpp` measures the sample-to-sample step at the
  off edge against the programme's own: Retro was 43x its steady state, and is now below it.
* **Space flushed its network and returned in the same block**, so the tail vanished in one
  sample and the dry leg jumped from its mix gain to unity. It now runs until its smoothed wet
  has actually reached zero. The same test caught this one on the first run after it was
  sharpened: 6.3x steady state, now 1.0x.
* **A slot the order did not contain was forced off but never called**, because the chain
  iterated `order.count`. Dragging a card out of the chain view starved exactly the history and
  delay lines the unconditional call exists to keep fed. The chain now runs the absent slots
  too; they are already forced off, so each takes its own exact-bypass path and does nothing
  but keep its lines current.
* **The low-end test did not include Grain**, which is the only engine in the chain that
  genuinely decorrelates — it pans each grain independently — so the single test standing behind
  the §38/40/43 claim was not exercising the case the claim exists for. It now enables all nine
  modules with Grain at full spread, and measures 0.962.
* **Retro still emits 4 ms of silence after `prepare()` or `reset()`**, because both its dry
  tap and its wet tap read from a transport that has not been written yet. It is the same 4 ms
  as its latency and it happens once per rate change.
* **`applyFxBypass` writes the six `*_on` flags through the same modulation overlay the mod
  matrix uses, and runs after it**, clearing the override on every live slot. The two cannot
  collide in practice — the MOD page's target menu offers float parameters only, so a routing
  can never name a power flag — but if that ever changes, this is the line that would silently
  discard it.
* **Both `getLatencySamples()` return their delay unconditionally**, and the chain gates the
  figure on the `*_on` flag. Since the engage crossfade below outlives the flag by about
  230 ms, the host is told zero for that long while the module is still delaying. Reporting a
  latency that is wrong for a fifth of a second after a button press was judged better than one
  that is wrong for as long as the module is on.
* **Retro's resampling grid aliases and is not oversampled.** Deliberate — it is what a 26 kHz
  12-bit sampler did — but ERA near 1 is not a clean stage, and the grid crossfades with the
  un-held signal as ERA morphs into it rather than the converter's rate sliding, which no real
  machine does. The dropout scheduler is memoryless, so two events can overlap. The ACETATE
  end's mono collapse reduces width: safe under §38/40/43, but a patch that needs its width
  back has to keep ERA below about 0.2.
* **Crush's bit depth is a block-rate constant**, so fast BITS automation moves in block-sized
  steps — steps in the step size, not in the signal, so they do not click, but a fast sweep is
  not smooth. The oversampled drive path runs even at DRIVE 0 so the latency does not move with
  a parameter. At 1 bit the quantiser has three levels (−1, 0, +1), not two.
* **Filter's MORPH wraps from FORMANT back to LP**, which is consistent but not obvious. Filter
  B runs only above MORPH 1e-4, so the first samples after MORPH leaves zero are a filter
  starting from rest (its gain there is zero, so nothing stale is heard). Each `SynthFilter`
  carries a 16 KB comb buffer whether or not the comb is selected, so the engine is about 66 KB
  of object. MOTION at full depth with RES near maximum sweeps a resonant peak across two
  octaves: stable, but loud.
* **Rewind has no UI or MIDI one-shot trigger.** There is no parameter for one, so it is absent
  rather than invented. Its history reads are linear-interpolated, so a fractional playback rate
  loses a little top end; it is not a transparent varispeed. A REVERSE gesture is shortened at
  high SPEED, because the tap digs back at `1 + SPEED` samples per sample. SPEED means "playback
  rate" in REVERSE and RETURN but "how fast the gesture happens" in STOP and DIVE.
* **Grain's reads are linearly interpolated**, which above about +12 semitones is audible as a
  dull top end; `HistoryBuffer` has no Hermite read although `DelayLine` does. Its pitch
  quantisation is relative, not key-aware: a fixed transposition cannot be diatonic for every
  note it is applied to. FREEZE takes 78 ms to engage. The gain law assumes every scheduled
  grain sounds, so dropped grains make the texture quieter than it expects, and grains still in
  flight are cut mid-window when the mix ramp reaches zero.
* **Space's wet level has never been calibrated** against any reference. Changing SIZE or
  PRE-DELAY sweeps the delay reads rather than crossfading between two of them, so a fast move
  glides the tail's pitch. The early reflection pattern is a fixed synthetic tap list, not an
  image-source model, and eight lines is a modest modal density: above about 2.5× scale the
  tail thins.
* **The quality parameter (ECO / STUDIO / ULTRA) is ignored by all six engines**, and **CPU has
  not been measured** for any of them: the synth core has a benchmark, the FX chain does not.
  Grain's cost is proportional to sounding grains — 40 at the top of DENSITY and SIZE — and
  there is no SIMD anywhere in the chain.
* **The chain tests in `Tests/Main.cpp` cover composition, not character:** order packing, parser
  robustness, a silent chain leaving the mid signal within −60 dB of the bare synth, everything-on
  at four sample rates staying finite and under a peak of 8, twelve random orders rendering, and
  the low-band correlation test above. They do not check any engine's algorithm, and none of them
  was run for this document.
* **This subsystem is not production-ready.** It contains no placeholder engines — all six do
  what they claim — but Retro's 4 ms bypass step, Space's unramped off edge and the removed-slot
  case are defects a user could find in a first session, and nothing here has been heard.
