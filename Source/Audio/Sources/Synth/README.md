# The NÁCAR synth

Specification §162 asks for this document: the oscillator algorithm, the
anti-aliasing strategy, the wavetable strategy, what MIRAGE, HAZE and MASS
actually do, the unison normalisation, the filter architecture, Body, Density,
the stereo architecture, low-end management, voice allocation, the oversampling
policy, CPU, and the known limitations.

Every number quoted below is measured by `NacarBench`, not estimated.

---

## Oscillators

**Bandlimiting is PolyBLEP**, a two-point polynomial residual added around each
discontinuity (`Oscillator.h`). Saw has one discontinuity per cycle, pulse has
two, and both are corrected. It costs four arithmetic operations and is exact at
DC.

**Triangle is a leaky integration of the bandlimited square.** Integrating a
bandlimited square gives a bandlimited triangle for free: the corners are
rounded by exactly the amount the BLEP rounded the edges. The integrator's
near-DC gain is about 1500× at its 5 Hz leak, so its output is high-passed — a
lesson learned the hard way, because phase-modulating the square puts a
low-frequency term into the integrator and the benchmark measured the result at
98 % of its energy below 200 Hz with a spectral centroid of 25 Hz.

**Hard sync uses a second, time-domain BLEP.** The reset is driven by another
oscillator, so the phase-domain residual cannot anticipate it; instead the
actual step the waveform takes at the reset instant is measured and its
two-point residual spread across this sample and the next.

**Sine** uses a 9th-order odd Taylor series folded into a quarter turn, about
−94 dB THD, because eight unison sub-voices calling `libm`'s `sin()` per sample
is not affordable.

### Measured aliasing

Inharmonic energy relative to harmonic, single note at C7 (2093 Hz), 48 kHz,
sustain only, Blackman-Harris window:

| Probe | |
|---|---|
| saw | **−40.3 dB** |
| pulse, 25 % width | **−39.4 dB** |
| wavetable, METALLIC at position 0.8 | **−32.9 dB** |
| saw + Body at 50 % | −40.4 dB (no contribution) |
| saw + pre-filter drive at 50 % | −39.8 dB (no contribution) |
| saw + post saturation at 50 % | −40.4 dB (no contribution) |
| saw + ladder filter drive at 60 % | −35.9 dB |

The figure is only reported for probes where it means something. Detuned unison,
FM, hard sync and chords all put energy off the harmonic series **by design**, so
reporting it there would be measuring the patch rather than the oscillator.

## Wavetables

Eight families, built procedurally at startup from a per-family spectral recipe
(`WavetableBank.cpp`). NÁCAR ships no wavetable assets: the tables are a pure
function of the code, so they cannot go missing and cannot be version-skewed
against a preset.

Each family holds 12 frames; each frame holds a 10-level mip pyramid where level
L contains no harmonic above `1024 >> L`. An oscillator picks the level whose
highest harmonic still fits under Nyquist for the note it is playing, so a table
never aliases however high it is transposed.

Three interpolations happen on every read and all three matter: cubic Hermite on
phase (linear in ECO), linear between the two frames either side of Position, and
linear between the two mip levels either side of the pitch. Without the last
one, a glissando steps from one bandwidth to the next and you hear the top
harmonics vanish.

The bank is immutable and shared. Building it per voice would be thirty-two
times the work for identical data.

## Unison

Five things have to be right at once: what the voices are tuned to, where they
sit, where in the cycle they start, how the group is normalised as the count
changes, and how far they may differ in anything else.

Five topologies — TIGHT, DENSE, WIDE, HAZE, CLOUD — differ in the *shape* of the
detune distribution, not its width. **Pan is correlated with detune, not
independent of it**: the sharp voices sit to one side and the flat voices to the
other, so the beating sweeps across the image instead of pulsing in place. That
single decision is what makes a unison group sound like one wide instrument
rather than several narrow ones.

### Normalisation

```
gain(N) = N ^ -(0.5 + 0.5 * c)

spreadHz = f0 * (2^(cents/1200) - 1)
c        = 1 / (1 + (spreadHz * 0.08)^2)
```

`c` is the correlation between sub-voices, 0 (independent) to 1 (identical). The
two limits are the easy part: N identical voices sum to N times one voice and
need `1/N`; N independent voices sum in power and need `1/sqrt(N)`. Every real
group sits between, and using either law alone is audible — `1/sqrt(N)` makes a
tightly detuned group jump 9 dB going from one voice to eight, and `1/N` makes a
widely detuned group sag by the same amount.

What decides where a group sits is how fast the voices beat, and that is a
**frequency in Hz, not a number of cents**: ten cents at 50 Hz is a 0.3 Hz beat
the ear integrates as one louder note, while ten cents at 2 kHz is a 12 Hz
flutter it integrates as two separate ones. The 0.08 s integration window is
used rather than the textbook 0.03 s because the question this formula answers is
"does the level jump", and a beat slower than about 12 Hz is heard as level while
a faster one is heard as texture.

At detune exactly zero the voices really are identical, `c` goes to 1, the
formula gives `1/N`, and unison correctly becomes a no-op instead of an 18 dB
boost.

## The three characters

They are not three synthesizers. MIRAGE, HAZE and MASS share every oscillator,
filter, envelope and voice in the engine; what differs is a struct of
behavioural coefficients the voice consults while it renders
(`SynthCharacter.h`). Anything not in that struct is identical across the three,
which is why a patch keeps its identity when you switch character.

| | MIRAGE | HAZE | MASS |
|---|---|---|---|
| unison topology | WIDE | HAZE, CLOUD above 5 voices | TIGHT, DENSE above 4 |
| max detune | 20 cents | 15 | 11 |
| drift / variation | 0.45 / 0.55 | 1.0 / 1.0 | 0.35 / 0.4 |
| default filter | state variable | state variable | ladder |
| saturation bias | clean | warm | thick |
| wavetable motion | 0.35 | 0.12 | 0 |
| width | 1.10 | 1.05 | 0.82 |

MASS is the only one that changes its unison topology with the voice count: two
or three sub-voices want TIGHT, but past five the only way to stay physically
large rather than becoming a chorus is to pull the tuning in and go DENSE.

## Filters

**MASS** is a zero-delay-feedback four-pole ladder with a saturator in the
feedback path. Zero-delay matters more here than anywhere else: a naive ladder
inserts one sample of delay into the feedback loop, which at high cutoffs
detunes the resonance and at high resonance makes self-oscillation sharp instead
of round. Solving the loop algebraically costs one division and removes the
error. The nonlinearity is in the feedback, not on the output — that is what
makes drive interact with resonance the way it should: driving a linear filter's
input only makes it louder, but driving its feedback compresses the resonant
peak as it grows, which is why a hard-driven ladder gets fatter rather than
shriller.

**HAZE** is a TPT state variable. All four responses fall out of the same two
integrators, so morphing between them is continuous and the notch is genuinely a
notch. Q runs 0.5 to 14 — it rings without ever self-oscillating, because the
smooth filter is not the one that should scream.

**COMB** and **FORMANT** serve MIRAGE. Formant morphs through five vowels whose
formant frequencies are ratios of the cutoff rather than absolutes, so the
character tracks the control instead of only working at one pitch.

### Stability

Specification §28 makes this a hard requirement, and two things enforce it:
cutoff is clamped to `[20 Hz, 0.45 × fs]` before any coefficient is computed, and
every sample is checked on the way out. A filter that has blown up resets its own
state and emits silence for one sample rather than poisoning the mix for the rest
of the session. `NacarTests` sweeps the cutoff across the full range at every
supported sample rate with resonance, drive, unison, sync and FM all at maximum
and asserts the output stays finite and bounded.

## Body and Density

**Body** is not a waveshaper on the full band — that would make a sound bigger
and dirtier at the same rate. The band that carries physical size, roughly
90 Hz to 700 Hz, is split out, shaped on its own and mixed back; the top of the
spectrum, where distortion is audible *as* distortion, is never touched. The
shaper is asymmetric, and that asymmetry generates the even harmonics: the
second harmonic of a 100 Hz fundamental is 200 Hz, squarely where a small
speaker can reproduce it. The benchmark confirms it contributes no measurable
aliasing.

**Density** is fullness without width: a short, fixed, mono comb, identical on
both channels, so it adds nothing to the stereo image and nothing to the mono
difference signal. The delay is deliberately fixed — modulating it would make it
a chorus, and a chorus is the one thing a centred MASS bass must not be.

## Stereo and the low end

Three bands, one rule each: **low collapsed to mono**, mid scaled by Width, high
scaled by High Width. The splits are TPT one-poles, complementary by
construction, so the stage is transparent when all three widths are 1.

A bass that is wide on a monitor is a bass that partially cancels on a club
system. Measured low-band L/R correlation across the benchmark set: **1.00** for
every bass, Reese and sub patch; 0.67 to 0.98 for pads and poly patches, which is
where width belongs. Energy retained when summed to mono is between **−0.0 and
−0.6 dB** across all twenty-nine benchmarks.

The sub is never panned, never detuned and never widened, and it is summed in
mono after the stereo sources.

## Voice allocation

Free voice first, then the quietest released one, then the quietest, then the
oldest. A voice still in its attack is never the first choice — stealing the note
someone just played is the most audible mistake an allocator can make. Stealing
uses a 4 ms fade rather than a hard cut.

POLY, MONO and LEGATO, with a held-note stack so releasing a note in a trill
falls back to the one still down. Glide in constant-time and constant-rate.

## Oversampling

Specification §59 asks for selective oversampling and for it to be justified by
profiling rather than applied blindly. The profile is the table at the top of
this document: Body and the post saturator contribute nothing, the drive stage
contributes +3.8 dB, and the ladder's feedback saturator contributes +9.4 dB.

So the block that runs at 2× is exactly

```
pre-filter drive -> primary filter -> creative filter -> saturator
```

and nothing else. The filter is a 19-tap polyphase halfband whose taps are
computed from a windowed sinc at startup rather than pasted in as a table
(`Halfband.h`). `NacarTests` proves it round-trips a sine at −85 dB residual and
rejects an out-of-band tone by 48 dB; without that test the half-sample
misalignment in the first version would have shipped, because it still produced
plausible audio.

After oversampling, the nonlinear stages add **no measurable aliasing at all**:
every single-stage probe sits at the bare oscillator's own floor.

**The block is not switched off in ECO.** A filter's coefficients are tied to the
rate it runs at, so turning oversampling off at runtime would retune every filter
in the instrument. ECO's savings come from the wavetable interpolation instead.

## CPU

48 kHz, 256-sample blocks, single threaded, every voice sounding at once — the
worst case, not a typical one. Measured on a cloud VM core, so treat the ratios
between rows as more meaningful than the absolute numbers.

| | realtime | one core |
|---|---|---|
| 1 voice, no unison | 34.2× | 2.9 % |
| 1 voice, 8× unison | 24.6× | 4.1 % |
| 8 voices, no unison | 4.4× | 22.7 % |
| 8 voices, 4× unison | 3.7× | 27.4 % |
| 16 voices, 4× unison | 1.9× | 52.1 % |
| 32 voices, 8× unison | 0.8× | 125.2 % |

Unison is nearly free because the per-sub-voice detune ratios and pan gains are
computed once per block; the fixed per-voice cost dominates, and roughly 45 % of
that is the oversampled block and its halfbands. **32 voices at 8× unison does
not run in realtime on this machine.** Polyphony defaults to 16.

Sources whose level is zero at both ends of a block are not rendered, so a patch
that leaves the auxiliary oscillator and the noise silent — most of them — pays
for neither.

---

## Known limitations

- **Nobody has listened to this.** Everything above is measured. Whether the
  instrument sounds expensive is not a measurable property and the benchmark
  says so on its own last line.
- **32 voices at 8× unison exceeds realtime** on the machine measured. The
  engine needs either SIMD in the oscillator inner loop or a cheaper oversampled
  block before that configuration is usable.
- **ECO does not reduce the oversampling**, for the reason given above. It only
  drops the wavetable phase interpolation from Hermite to linear.
- **ULTRA is accepted and stored but behaves as STUDIO.** 4× oversampling is not
  implemented.
- **The wavetable probe aliases about 7 dB worse than the analogue waveforms**
  (−32.9 dB against −40.3 dB) on the METALLIC family at high positions. The mip
  level is chosen from the phase increment alone; a table whose upper frames are
  brighter than its lower ones is under-protected at the frame boundary. The fix
  is a per-frame harmonic count rather than a per-family one.
- **FM is not through-zero.** The oscillator clamps its increment to
  `[0, 0.45]`, which is what makes it safe to modulate at audio rate; the cost is
  that negative frequencies fold rather than reflect.
- **No SIMD anywhere.** The unison inner loop is the obvious candidate.
- **`Quality::ultra` and `setOfflineRendering` reach the engine and are read**,
  but currently only select the wavetable interpolation.
