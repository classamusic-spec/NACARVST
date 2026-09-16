# NÁCAR Weight — §74

The last stage in the instrument. It makes the sound feel physically larger in one
chosen part of the spectrum: `weight_mode` chooses where, `macro_weight` chooses how
much, `weight_harmonics` and `weight_compress` shape it. It runs after Patina and
before `NacarEngine`'s output stage, and it is deliberately macro-neutral — every
other engine has already applied its share of age, grit, movement and scale.

Nobody has listened to this. Everything below describes what the code does.

---

## SUB — the bottom, without the bottom

Adding level below 60 Hz does nothing on most playback systems and eats all the
headroom, so SUB works by **harmonic reinforcement**: it generates the harmonics
that let the ear infer a bass note the speaker cannot reproduce. The second harmonic
of a 40 Hz fundamental is 80 Hz, squarely where a phone speaker lives.

The generator is a **Chebyshev pair** on a level-normalised copy of the low band
(split at 110 Hz): `T2(x) = 2x² - 1` and `T4(x) = 8x⁴ - 8x² + 1`. For a sinusoidal
fundamental `T2(cos t) = cos 2t` and `T4(cos t) = cos 4t` — exact harmonics, at
exactly the right phase, with no delay anywhere in the generator.
`weight_harmonics` sets how much is generated and slides the mix from pure T2 towards
`0.55·T2 + 0.75·T4`.

**Phase awareness is structural.** Both polynomials are *even*, so the generator emits
nothing at the fundamental at any amplitude for any input and cannot thin the note it
is reinforcing. That is why T3 is not used: the odd Chebyshevs stay free of a
fundamental term only at exactly unit amplitude, and a generator that emits a
negative-going fundamental when its normalisation is off by a decibel is the exact
failure being guarded against. The generator is memoryless, so its harmonics leave
phase-locked to the waveform that produced them; the three first-order filters after
it (6 Hz DC blocker, 55 Hz highpass, 600 Hz lowpass) rotate the second harmonic of a
40 Hz note by about 46 degrees, which can move the crest factor of the sum but cannot
make the harmonics cancel the note — nothing downstream of an even generator can
create a fundamental component.

Normalisation is a **1 ms / 150 ms peak follower**, not an averager: it reaches a new
note's level within a fraction of a cycle and holds it through the decay. When a
follower lags, the normalised copy clips, and a clipped even polynomial is a constant
— which is to say a thump. SUB also applies at most **+2.3 dB** of linear lift to the
low band and runs the band compressor inside it; the lift is small on purpose,
because the weight is supposed to come from the harmonics.

## BODY — where physical size lives

150–700 Hz, split out with `fx::ThreeBand` and shaped on its own, because a
waveshaper on the full band makes a sound bigger and dirtier at the same rate. The
shaper is `tanh` with an asymmetric alternative, crossfaded by `weight_harmonics`: at
0 the mode is a dynamic band lift, at 1 it is harmonic saturation. Drive is at most
2.1× and **the shaper divides its own drive back out**, so driving it harder changes
the curvature and not the level. This is not `synth::SynthBody` reused — that splits
at 90 Hz with up to 3.6× of drive for one voice's oscillator sum, while this runs on
a finished mix, so the split moves up, the drive comes down, and the dynamics are
**linked across the channels** so the image cannot move. The low band gets a small
purely linear lift and nothing else: mass down there is amplitude, not harmonics.

## AIR — detail, not level

Boosting 12 kHz on a signal with nothing at 12 kHz only raises noise, so AIR does two
things: a gentle shelf above 6 kHz (at most +3.2 dB) and a generator that *makes* the
top. The generator takes the 2–5.5 kHz band, squares it against its own envelope and
highpasses the result twice at 6 kHz. Squaring a band whose content lies below fs/4
gives sum and difference products that are all below Nyquist by construction, which
is why the source band stops at 5.5 kHz — at 44.1 kHz, fs/4 is 11 kHz. The band
compressor runs on the high band, so what comes up is the top of a note's *decay*
rather than of its transient, which is where air lives. It is not a de-esser.

## The band compressor

One design, used by all three modes on their own band. The threshold is **half of the
band's own recent level**, not an absolute one, so the stage behaves the same at
−20 dBFS as at −6. Detection is feed-forward, no lookahead, linked across the
channels; the detector releases in 120 ms, the reference in 400 ms, and **both attack
in 8 ms** (25 ms in SUB, so the detector measures the note and not the waveform).
Ratio runs 1:1 to 2.5:1.

The shared attack is the point: during any rise the two followers are identical, so
the detector sits at exactly twice the threshold — where the makeup is normalised —
and the gain is exactly 1.0, from silence, from a cold follower, from anywhere. A
reference that lagged the detector would collapse the gain every time a note started
from nothing. After a peak the detector falls while the reference holds, so the tail
of a note is lifted. **It can only ever raise**, by at most 3.6 dB, and its threshold
comes from the input band and never the output, so there is no feedback path here.

---

## Gain staging — §148, "do not use loudness to fake quality"

Three layers. **Structural**: each stage divides out the gain it knows it added — the
BODY shaper divides by its own drive, the compressor's makeup is normalised at its
operating point rather than at its threshold. **Feed-forward**: each mode's
`powerRatio()` reports the broadband power ratio it expects to have produced, from
its own controls and a fixed assumption about a mix's balance (35 % below 110 Hz,
40 % in the low mids, 12 % above 6 kHz), and the reciprocal square root is applied as
a static trim. **Measured residual**: two loudness meters, input and output, each a
K-weighting-like pre-filter (two poles of highpass at 70 Hz plus about +4 dB of shelf
above 1.5 kHz) into a 1.2 s mean square, giving a trim of
`sqrt(inputMeanSquare / outputMeanSquare)` updated once per block and ramped across
it. The weighting matters: an unweighted meter would hear SUB's new low end as
loudness and pull the instrument down for adding exactly what it was asked to add.
All of it multiplies into one gain that is itself faded in by `macro_weight`, so at
zero the gain is exactly 1.0.

Now the honest part. There are **two** clamps, and they were one until a review of
this file pointed out that a single clamp on the product let a wrong feed-forward
estimate silently eat the measured correction's whole budget. The measured residual
is clamped to ±3 dB on its own; their product is then clamped again, **asymmetrically,
to −3.1 dB / +1.5 dB**.

The asymmetry is §148 — *do not use loudness to fake quality*. The downward direction
is the safety, and it is the only thing in the engine that can reduce gain at all,
since the band compressor never does. The upward direction exists only to undo an
over-conservative feed-forward estimate, and a stage whose job is physical mass must
not be able to win an A/B by being louder. The cost is stated rather than hidden: **a
patch the feed-forward badly mis-estimates, which genuinely needs more than 1.5 dB of
restoration, will not get it** and will come out quiet. Being too quiet is the safer
of the two errors.

Nothing has measured whether the match holds: there is no loudness test for Weight in
`Tests/Main.cpp`. The only level evidence is the chain benchmark's peak and RMS
columns, which cover the whole instrument rather than this stage. Nobody has listened
to it.

---

## Bypass

`NacarEngine` calls Weight on every block whatever `macro_weight` says, for the same
reason it calls every engine unconditionally: an engine that is not called cannot see
the transition it needs in order to manage its own state, and gating the call would
click on the power button.

With `macro_weight` at exactly zero **and** the smoothed amount already below 1e-4,
`process()` returns before touching the buffer — bit-exact. Before the fade runs out
the path is exact by construction rather than by arithmetic: every mode is written as
`in + amount · (something)` over complementary band splits and the output gain is
`1 + amount · (trim - 1)`, so `amount = 0` is the identity. A complementary split
reconstructs to within one float ulp rather than bitwise, which the file header puts
at about 1.5e-8 (−153 dB) on a 0.7 amplitude signal.

While bypassed, both loudness meters are still fed — with the same bypassed samples,
so their ratio holds at exactly 1. They used to be updated only inside the sample
loop, which meant that after a bypass they held a mean square from whatever had been
playing before it and the first re-engaged block corrected for the wrong programme.

Two things to know. The early-out test is `amountTarget <= 0.0f`, strictly zero, so
any non-zero `macro_weight` however small runs the whole stage; and the amount fade
is a one-pole with a 20 ms time constant against a 1e-4 threshold, so the engine keeps
running for roughly 180 ms after the control reaches zero. `macro_weight` defaults to
**0.35** with BODY as the default mode, so Weight is doing something in a default
session — off is not the default. Mode changes crossfade over 12 ms, and a mode coming
back is woken with its envelopes seeded from the measured programme level so it errs
towards doing nothing rather than towards a dip.

---

## The low end — §38, §40, §43

Weight is the last engine that can touch the bass, and it is written so that it
cannot change the low end's stereo image in either direction.

- **SUB's generator runs on the mono sum** of the two low bands and its output is
  added equally to both channels, so what it adds is perfectly correlated and survives
  a mono fold-down intact.
- **The existing low band is never collapsed and never widened** — only ever gained,
  identically in both channels, in both SUB and BODY.
- **Everything SUB adds lands between 55 and 600 Hz.** Adding energy below 55 Hz is
  the thing the mode exists to avoid; the 6 Hz and 55 Hz highpasses are there for that
  as much as for the DC an even nonlinearity generates.
- **BODY's band starts at 150 Hz**, so a mix's bass fundamentals stay out of the
  shaper, and its compressor gain is linked so the dynamics cannot move the image.

One qualification. BODY's shaper is a memoryless curve applied per channel, so a mono
input stays mono — but a nonlinearity is not a linear operator, and a partially
correlated 150–700 Hz band need not keep exactly the correlation it arrived with.
That band sits *above* the chain's backstop: `NacarEngine::outputStage` splits at
140 / 2600 Hz and writes `(lowL + lowR)/2` into both channels unconditionally,
whatever the width is. The backstop is real, but it is below where BODY works, so it
would not catch this.

---

## Macros, parameters, realtime

Weight reads `macro_weight`, `weight_mode`, `weight_harmonics` and `weight_compress`
— the whole parameter block, none of it accepted and ignored. `MacroState::weight`
and `MacroState::weightMode` are deliberately ignored in favour of the registry, which
is authoritative and guaranteed populated; no other macro field and no Pulse
destination is read.

§146: after `prepare()`, `process()` allocates nothing, locks nothing, does no IO and
no logging. The registry is read once per block into locals, both smoothers are
block-rate with a per-sample ramp, and every output sample goes through `fx::guard()`.
Every divisor is floored — `e + 1e-6` in SUB's normalisation, `e + 1e-5` in AIR's,
`jmax(1e-5, 0.5·s)` for the compressor threshold, `jmax(1e-6, Σ modeGain)` for the
crossfade — so no parameter extreme can divide by zero. Weight adds no latency and
does not appear in `NacarEngine::updateLatency`.

---

## Known limitations — the honest list

- **Nobody has listened to this**, and no test anywhere measures its level behaviour.
- **SUB's generator is a nonlinearity on a summed low band.** With two bass notes at
  once it produces intermodulation products as well as harmonics, including a
  difference tone below both fundamentals. The 110 Hz split and the 600 Hz lowpass
  bound where the products land but do not remove them.
- **SUB still has a residual low-frequency artefact at a note's onset.** An even
  nonlinearity generates DC, the amount depends on how well the normalisation is
  tracking, and no follower tracks the first part of the first cycle. The peak
  follower and the two highpasses reduce it by roughly 16 dB at 10 Hz and push the
  rest above 55 Hz — a reduction, not a removal, and it has not been measured.
- **Nothing here is oversampled.** BODY's shaper runs on a band limited to 700 Hz by a
  one-pole, but the leakage above that corner is shaped by a nonlinearity at the base
  rate. AIR's generator is safe by construction inside its source band and unproven
  for the leakage above it: two poles at 5.5 kHz put the leakage at 11 kHz about 16 dB
  down, so its squared products land about 32 dB below the generated air — a
  calculation, not a measurement.
- **The feed-forward trim assumes a fixed spectral balance.** A patch that is all sub,
  or all air, is mis-estimated, and the measured residual then has to do the work
  inside the ±3 dB of its own and the −3.1 / +1.5 dB of the product clamp. A patch
  that needs more restoration than that comes out quiet.
- **BODY's `powerRatio()` ignores `weight_harmonics` entirely** — the parameter is
  commented out of the signature — although it moves both the drive and the asymmetric
  bias in `process()`. The shaper divides its small-signal gain back out, so to first
  order harmonics changes the band's spectrum without changing its power; the residual
  it does add is left to the measured trim. This is now stated as an exception in both
  the header and the function, rather than covered by a "line for line" claim that was
  not true of that mode.
- **The measured residual cannot distinguish "Weight made this louder" from "the
  player played louder"** over a 1.2 s window when the two coincide; a step change in
  playing level produces a slow gain drift of up to 3 dB until it settles.
- **The loudness meters are fed during bypass with the bypassed samples**, so their
  ratio holds at 1 rather than going stale — but a 1.2 s window means a long bypass
  leaves both meters describing the dry signal, and the first second after re-engaging
  is a correction that is still catching up.
- **The trim is broadband.** It multiplies the whole output, including the bands the
  selected mode never touched.
- **A mode that is not sounding keeps its filter state frozen.** Waking it seeds the
  envelopes and fades in over 12 ms, which covers it, but the first few milliseconds
  after a mode change are running filters whose state is older than the crossfade.
- **The band compressor has no lookahead and no soft knee** in the classical sense —
  the knee comes from the relative threshold, not the curve — and it cannot reduce
  gain at all. Anything that needs a peak held down needs a limiter, which is not this
  engine's job and does not exist elsewhere in the chain.
- **Weight keeps running for roughly 180 ms after `macro_weight` reaches zero**, and
  only an exact zero reaches the early-out at all.
