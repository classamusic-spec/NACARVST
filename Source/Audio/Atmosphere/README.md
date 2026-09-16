# The NÁCAR atmosphere layer

Three engines, in this order, after the FX chain and before Weight:

```
ShadowEngine.h/.cpp   an atmospheric duplicate behind the original   §93
AuraEngine.h/.cpp     the environment everything sits in             §92
PatinaEngine.h/.cpp   the surface the object has ended up with       §95
```

`NacarEngine` calls all three on every block, unconditionally, whatever their
enable parameters say. Each one owns its own bypass, which is why every engine
here has an exact early-out instead of relying on the chain to skip it. The
reasoning is under *Bypass*.

Nobody has listened to any of this. Everything below describes what the code
does, not how it sounds.

---

## Shadow — §93

> "not simply chorus, delay or reverb"

**Additive.** The dry path is never touched at any setting; the output is
`in + shadow · level · duck`, so the original leaves bit-exact and LEVEL at zero
is a true bypass rather than a very quiet effect.

**The duplicate is the mono sum**, written to one delay line. LENGTH is
`25 · 2^(p·5.1699)` ms — 25 ms to 900 ms.

**The pitch shifter is two-pointer overlap-add.** The pointers sit at `base + p·W`
and `base + frac(p + 0.5)·W`, with `p` advancing by `(1 - ratio)/W` per sample; a
pointer whose delay changes at that rate plays back at `ratio` times speed. Each
gets a raised cosine half a period out of phase with the other, so the two gains
sum to exactly 1 and the crossfade adds no amplitude ripple. What it does add is a comb
between two copies at different delays during the overlap — inherent to time-domain
shifting, and why this is a smeared duplicate rather than a clean transposition. The
window is 60 ms up to a semitone, rising linearly to 120 ms at an octave and held
there to ±24; the artefact rate `|1 - ratio|/W` is 8.3 Hz at +12 with the long window
against 16.7 Hz with the short one, and Shadow takes the long one because it is a
blurred object by definition. At PITCH near zero the pointers would comb-filter
permanently at the parameter's own default, so the shifter is crossfaded against a
single unshifted tap over the first semitone, and its two reads are skipped entirely
while that blend is below 1e-4.

**The envelope lag** is four lines and does the most work. Two followers on the
duplicate, 1 ms / 60 ms against 55 ms / 500 ms, give
`1 - lagDepth · (1 - min(1, slow/fast))`, `lagDepth` running 0.45 to 0.80 with
DISTANCE: steady state is 1, an onset dips it, a decay is untouched because the ratio
is clamped at 1. A transient is softened by 5 to 14 dB and swells back over about
55 ms. Deliberately not a full duck — a shadow that vanishes on every attack reads as
a broken gate, one with no lag reads as a chorus.

**BLUR smears in time.** Four modulated allpasses per channel (13.7–47.1 ms left,
17.3–53.3 ms right, coefficient `0.15 + 0.66·BLUR` clamped to 0.10…0.84, lengths
scaled 0.50…1.30, each modulated at 0.061–0.191 Hz). The channels share no length,
and that difference is the only thing giving a mono-sourced duplicate any width,
which is why a clean shadow is also a narrow one. After them a **cross-coupled
tank**: each channel's feedback comes from the *other* channel's line (43.1 and
57.7 ms) through a 3200 Hz lowpass into an 11.3 / 14.9 ms allpass, at `0.60·BLUR`.
The pair's loop gain is therefore at most 0.60 — about an 800 ms decay, long
enough to be a genuine smear, short enough that Shadow is not a second reverb.

**DISTANCE is not a level control**; LEVEL is. It lowpasses from 9 kHz down to
about 1.4 kHz, highpasses from 120 Hz up to 320 Hz, narrows by up to 55 %, deepens
the envelope lag, and only then trims the level by up to 25 %.

**Macros.** `scale` +0.20 on LENGTH; `distance` +0.30 on DISTANCE, so World reaches
those five cues rather than a wet level; `movement` +0.25 on BLUR and +0.5 ms on
its modulation; `wetBias` +0.15 on LEVEL; `widthScale` multiplies the duplicate's
width; `pulseToSpace · pulseAt(i)` ducks it by up to 85 % per sample; `breathAt(i)`
drifts LENGTH by ±0.3 %, detuning the duplicate a few cents as it moves. `age`,
`grit` and `alterAmount` are not read. **The parameter is the gate**: at
`shadow_level` zero the buffer comes back untouched however far World is turned up.

---

## Aura — §92

Space is the room the sound is in; Aura is the atmosphere the room is in. In code:
no early reflections at all, no mix parameter, always in the path, and a tail read
back twice.

```
in ─ air absorption ──────────────────────────────────────────> direct
  └─ 130 Hz highpass ─ pre-delay (35 ms → 6 ms with DISTANCE)
                         └─ 3 long modulated allpasses per channel
                              └─ 4-line FDN, Hadamard, damping + 55 Hz cut in loop
                                   ├─ tail, direct
                                   └─ 1.2 s history → 4 Hann grains
                                        └─ FOG crossfades the two
                                             └─ tilt → three-band width
out = direct·dryGain·directTrim + wet·wetGain·duck
```

**The network** is four delay lines at 97.15 / 131.23 / 174.52 / 229.23 ms — the
primes 4663, 6299, 8377 and 11003 at 48 kHz, because mutually incommensurate
lengths do not share modes and a shared mode is a ring. Four rather than eight
because the density a reverb gets from many lines, Aura gets from the allpasses in
front and the grains behind, and four long lines cost about a third of what eight
short ones do — which matters for an engine that is always on. SIZE scales them by
`0.45 · 2^(size·2.078)`, clamped 0.45…1.90, so the longest runs 103 ms to 436 ms.
Mixing is an orthonormal 4-point Hadamard, so all the loop gain sits in the
explicit per-line gain `exp(-3 ln10 · T / RT60)`, clamped to 0.9992. RT60 is
`1.2 · 2^(decay·3.544)` — 1.2 s to 14 s, inside the 32 s tail the processor
promises the host. There is no INFINITE and nothing approaches unity; a soft
ceiling, transparent below 1.5 and asymptotic to 3.0, sits in the loop as
insurance. **Damping is in the feedback path**, which is what makes the atmosphere
darken as it fades instead of being dull from the start:
`5200 · 2^(light·1.2) · (1 - 0.5·fog) · (1 - 0.3·distance)`, clamped to
300 Hz…0.45·fs, spread 1.00 / 0.90 / 1.10 / 0.95 across the lines, with a 55 Hz
highpass in the loop beside it.

**The diffusers are the opposite decision from a reverb's**: three long modulated
allpasses per channel, 23.3–58.1 ms left and 29.7–67.3 ms right, coefficient
`0.55 + 0.30·FOG + 0.10·distance` clamped to 0.30…0.86, modulated at
0.043–0.107 Hz. They turn an impulse into a smear before the network sees it.

**The granular tail.** The network's stereo output is written to a 1.2 s history and
read back by four overlapping Hann grains of 70–310 ms (`70 + 180·size + 60·fog`,
times a random 0.7…1.3), each starting 40–640 ms behind the write head at a rate
within ±0.15 % of unity and a random pan of ±0.8. Because the rate is within a
thousandth of unity a grain cannot close the gap to the writer during its life, so
neither bound is ever near; the window is zero at both ends, so a grain fades rather
than clicks. They are **feed-forward only** — feeding them back would make a more
interesting engine whose loop gain the author could not bound — and they do not
transpose, because an octave-up grain cloud is a specific and recognisable effect,
not an atmosphere. FOG sets how much of what you hear is the grains:
`late = 1 - 0.45·fog` on the direct tail against `cloud = 0.18 + 0.55·fog`.

**DISTANCE, and §73.** DISTANCE is Aura's amount control, which is exactly the trap
§73 warns about, so the amount is the *last* thing it does. In order: the direct
sound loses highs, a 3.6 kHz one-pole blended in up to 80 %; the internal pre-delay
*shortens*, 35 ms to 6 ms, because a distant source's environment arrives close
behind it; the smear deepens; the image narrows by up to 30 %; the tail darkens by
up to 30 %; and only then the wet level, on `0.85 · a · (0.45 + 0.55a)`,
deliberately slow at the bottom so the first third of the control is almost
entirely the other five cues and almost no level. The direct trim and the air
absorption are scaled by how present the wet is, so they reach zero exactly as the
wet does.

World reaches this twice over. The macro resolver already splits World into four
fields (`scale = w`, `distance = w²·0.9`, `wetBias = w·0.25`,
`widthScale = 0.85 + 0.45w`), and Aura adds `0.30·distance + 0.20·wetBias` to DISTANCE
and `0.30·scale` to SIZE. World's total contribution to the amount is therefore at
most 0.32, and that amount is the six-cue control above rather than a mix. Be precise
about what "not only wet level" means here: World *does* move wet level, as the sixth
and slowest of six things, and `wetBias` is explicitly an offset on a mix.

**Macros.** `scale` +0.30 on SIZE; `distance` +0.30 and `wetBias` +0.20 on the
amount; `movement` +0.60 ms on the tail modulation and +0.50 ms on the smear;
`widthScale` multiplies the wet width as `widthScale · (1.05 - 0.30·amount)`;
`pulseToSpace · pulseAt(i)` ducks the wet by up to 85 % per sample; `breathAt(i)`
drifts every delay length by ±0.6 % and the grain offsets with it. `age`, `grit`
and `alterAmount` are not read — Aura is not an ageing engine and has no alternate
identity. **The parameter is the gate**: at `aura_distance` zero the buffer comes
back untouched however far World is turned up. World may colour an effect; it may
not summon one.

---

## Patina — §95

Memory is how many times the sound was copied, Retro is which machine played it
back, Patina is the surface it ended up with — thin and broad rather than deep and
specific, texture rather than damage, never completely still. That brief is also
what is *not* here: no wow, no flutter, no dropouts, no pitch instability, no
generation loss.

Per channel: four-band split at 140 / 420 / 2600 Hz → one static gain per band from
the profile → WEAR → recombine and HAZE smear → TONE → NOISE → band-gain makeup,
guard, engage crossfade against the dry input. The split is `fx::ThreeBand` with an
`fx::TwoBand` inside its mid band; both are complementary TPT one-poles, so the
bands sum back to the input and the stage is transparent by construction rather
than by trimming — to within one float ulp, about −153 dB, not bitwise. The
bit-exact path is the early-out.

**WEAR is erosion of detail**, three mechanisms confined to the top half of the
spectrum. A *detail ceiling*: a one-pole on the high band whose corner falls from
the profile's `ceilingHz` to its `ceilingFloorHz`. *Erosion*: downward expansion of
the high band, `(env + 0.35·thr)/(env + thr)`, so loud high-frequency material
passes and low-level detail is worn away; the threshold is a fraction of a slow
envelope of the programme rather than an absolute level, so the effect does not
change when the patch gets louder, and the 0.35 floor bounds the attenuation at
−9 dB so it can never become a gate. *Blunting*: a slew-rate limit on the
420 Hz…2.6 kHz band, proportional to the same reference envelope, zero in three of
the six profiles.

**NOISE is not a bed** — four things that all move. *Colour*: one
`synth::NoiseExciter` type per profile, WHITE for CHROME so its ring has something
flat to sit on, TEXTURE for SMOKE, PINK for the rest. *Ducking*:
`1 - duck · env/(env + 0.02)` on a fast envelope, with an absolute knee, because a
real noise floor is a fixed level that loud material masks. *Excitement*: the
opposite term — a proportion of the noise driven *by* the material instead of
hidden by it, 0.65 in CHROME against 0.60 of ducking in VINTAGE. *Drift*: the noise
level and the top of its band move with the surface drift, which is most of what
SMOKE is. CHROME's metallic edge is a two-pole TPT bandpass in the noise path,
normalised by `1/Q` so the ring does not change the level.

**The six surfaces.** A profile is not six intensities of one effect; every field is
declared once in an X-macro list, so the struct, the 40 ms coefficient morph and the
Alter blend are generated from it and a new field cannot be left out of one of them.
SOFT is handled rather than aged. VINTAGE is warm and low-mid forward with an even
hiss. CHROME is hard and bright and gets *scratched* rather than going dull. HAZE is a
veil made of phase, not filtering — the only profile where WEAR reads as diffusion.
SMOKE is dark, the most eroded, and its floor moves bodily. CUSTOM is flat.

**Drift** is `synth::DriftGenerator` — two sines at a near-golden ratio, bounded by
construction rather than clamped — at `driftRateHz · (1 + 0.8·movement)` clamped to
0.01…0.60 Hz, blended with `macros.breathAt(i)` in proportion to Motion. It moves
the tilt, the detail ceiling, the noise level, the noise band top and the smear
coefficient, and it modulates **filters, never a delay**: nothing here changes the
arrival time of anything.

**Macros.** `age` +0.35 on WEAR; `grit` +0.30 on NOISE; `movement` +0.30 on DRIFT
plus up to +80 % drift rate and up to 35 % of Breath blended into it; `alterAmount`
morphs the surface up to 60 % towards an alternate — SOFT→HAZE, VINTAGE→SMOKE,
CHROME→VINTAGE, HAZE→SMOKE, SMOKE→VINTAGE, CUSTOM having none because that is the
one the user is driving. `scale`, `distance`, `wetBias`, `widthScale` and every
Pulse depth are **not** read: Patina is a surface, not a space, and it has no width
stage to scale.

---

## Bypass

The chain calls all three on every block whatever their enables say. Gating the
call would click on the power button for the engines that must keep their lines
fed, and would mean **a reverb never sees the transition it needs in order to flush
its tail** — an engine that is not called cannot notice it was switched off, so it
would resume minutes-old audio when it came back.

| engine | while off |
|---|---|
| **Shadow** | `shadow_on` false or `shadow_level` ≤ 1e-5: flush the source line, the blur allpasses, the tank, the tone filters and both followers on the transition, then return. Additive, so the buffer is bit-exact. |
| **Aura** | `aura_on` false or `aura_distance` ≤ 1e-5: flush the four lines, the damping and loop cuts, the pre-delay, the smear allpasses, the width split, the tilt and the tail history on the transition, then return. Bit-exact. |
| **Patina** | `patina_on` false: the engage crossfade runs down against the dry input, and only once it is below 1e-4 does `process()` return early. Off *and* faded out is bit-exact; the fade itself is a lerp against the untouched dry sample. |

Two caveats. Patina's fade is a one-pole with an 8 ms time constant against a 1e-4
threshold, so it keeps running its full per-sample path for roughly 70 ms after being
switched off, not 8 ms. And **the `INTEGRATION NOTE` at the bottom of `AuraEngine.cpp`
and `ShadowEngine.cpp` is out of date**: it says the chain gates those engines and
that switching one off and on again resumes the old tail. `NacarEngine::Impl::process`
calls all four unconditionally, so the flush above does happen. Those comments are the
only thing in either file describing behaviour the code does not have.

Patina is also **not gated on its four controls**: with `patina_on` true and TONE,
NOISE, WEAR and DRIFT at zero the engine still runs the whole path, and a
non-CUSTOM profile still applies its band gains and makeup. Deliberate — the
profile *is* a colour — but "all controls at zero" is not "no Patina", and Patina
costs full CPU whenever it is on. `aura_on` and `patina_on` default to true;
`shadow_on` defaults to false.

---

## Gain staging

None of the three has a makeup gain in the usual sense. **Shadow** only ever adds,
at `level · 0.90 · (1 - 0.25·distance)`; the dry is untouched, so nothing it does
can be mistaken for the original getting better. **Aura** has no mix control, so its
dry/wet is an equal-power crossfade on the derived amount — at full DISTANCE
`mixEff` reaches 0.85, putting the dry around −12.6 dB before the direct trim takes
up to 2 dB more. That is a large level and tonal change, and the wet level **has not
been calibrated against anything**: no reference, no meter, no measurement behind
the numbers. **Patina** compensates its own band gains with
`makeup = 1/sqrt(Σ w[i]·gain[i]²)` over an assumed 30 / 32 / 26 / 12 percent power
distribution, clamped to 0.7…1.4; the assumption is fixed and wrong for any
individual patch, and exists so that switching profile is a change of colour rather
than of level. TONE needs no compensation because `fx::Tilt` pivots.

None of the three reports latency or appears in `NacarEngine::updateLatency` — they
add none. Shadow's LENGTH is a delayed *addition*, not a delay of the signal.

---

## The low end — §38, §40, §43

Width is never bought at the cost of the low end, and reverb is the single biggest
threat to that rule.

| engine | what it does |
|---|---|
| **Shadow** | highpasses the duplicate at 120 Hz rising to 320 Hz with DISTANCE — a distant small object has no bottom. What survives is split at 170 Hz and its low band **collapsed to mono**, so the shadow's bottom is perfectly correlated and can only add to the original's, never cancel it on a fold-down. |
| **Aura** | highpasses the reverb feed at **130 Hz** — higher than Space's 72 Hz, because an atmosphere has no bass in it at all, and it is the cheapest possible way to obey the rule in an always-on engine. A further 55 Hz highpass runs inside the loop. The wet is split at 160 Hz and its low band collapsed to mono before being added; the direct path keeps its low end and is never widened. |
| **Patina** | never erodes, blunts or smears the low band. The HAZE smear allpasses use identical coefficients in both channels, so they smear phase without widening. The noise is one **correlated mono core** plus a per-channel side component highpassed twice at 900 Hz, so nothing decorrelated reaches the low end; the core's own band bottoms out at 300 Hz in SMOKE. Drift moves filters, never delays. |
| **the chain** | `NacarEngine::outputStage` splits at 140 / 2600 Hz and writes `(lowL + lowR)/2` into both channels **unconditionally**, whatever the width is. Mid and high are scaled by `widthScale · (1 - widthDuck · pulseToWidth)`; the low band is not in that expression at all. |

Be clear about which protection lives where. The output stage is a **backstop, not
the mechanism**: it guarantees the instrument's output is mono below 140 Hz and
would silently rescue an engine that decorrelated the bass, but it runs once, after
everything, and it cannot undo cancellation that has already happened inside an
engine's own sum. That is why each engine mono-izes its own low band before adding
it rather than leaving it to the chain.

Two qualifications. The splits are first-order complementary one-poles, so "the low
band is mono" is a 6 dB/octave statement, not a brick wall. And the only measurement
of any of this is a chain-level test in `Tests/Main.cpp` — "the chain does not
decorrelate the low end", which renders with Retro, Filter, Space, Aura, Shadow and
Patina on and World at 1, lowpasses both channels at 150 Hz and asserts a
correlation above 0.85. It is chain-level, not per-engine, and **I have not run it**.

---

## Realtime guarantees — §146

After `prepare()`, nothing in any of the three `process()` bodies allocates, locks,
touches the filesystem, logs, or constructs a `juce::String`.

- **Every buffer is sized in `prepare()`**: Aura's four delay lines, pre-delay, six
  smear allpasses and 1.2 s tail history; Shadow's source line, eight blur
  allpasses, two tank allpasses and two smear lines; Patina's four allpasses. All
  are dimensioned for the maximum scale at the *highest supported* sample rate
  (`max(sampleRate, 96000)`), so a length cannot exceed its buffer at any parameter
  setting, and every delay read is clamped inside `fx::DelayLine` on top of that.
- **The registry is read exactly once per block**, into locals, before the sample
  loop. Each engine clamps `numSamples` to `min(macros.numSamples, buffer size)`, so
  an oversized block cannot run off the end of anything.
- **Aura restarts grains inside the audio loop and allocates nothing**: a `Grain` is
  six floats, and `restartGrain` touches only those and an `fx::Rng`.
- **Randomness is deterministic.** Every RNG is seeded from a constant in
  `prepare()`, never from a clock, so a session renders identically on every machine
  and in every offline render.
- **Nothing can divide by zero.** Shadow's envelope ratio adds 1e-5 to its divisor
  and clamps the result, window lengths are floored at 16 samples, Patina's erosion
  adds an epsilon to both halves of its quotient, and every cutoff is clamped before a
  coefficient is computed.
- **Non-finite values cannot propagate.** Input is `fx::guard`ed on the way in; Aura
  and Shadow check their wet sum for finiteness *every sample* and zero their own state
  rather than poisoning the session; every output sample is guarded again.

Every parameter in the ATMOSPHERE section of `ParameterList.h` is read and used —
`aura_on/_size/_distance/_fog/_decay/_light`,
`shadow_on/_length/_distance/_blur/_pitch/_level`,
`patina_on/_profile/_tone/_noise/_wear/_drift` — and none is accepted and ignored.
The global `quality` parameter is read by none of the three.

---

## Known limitations — the honest list

- **Nobody has listened to any of this.** Every claim above is an argument from
  construction. The only measurements touching these engines are the chain-level
  tests in `Tests/Main.cpp` (silent-chain transparency below −60 dB, low-band
  correlation above 0.85, finite and bounded at four sample rates), and I have not
  run them.
- **CPU has not been measured for any of the three.** Aura and Patina are on by
  default, so whatever they cost is paid by every patch.
- **The `INTEGRATION NOTE` comments in `AuraEngine.cpp` and `ShadowEngine.cpp` are
  wrong.** They describe a chain that gates those engines; the chain calls them
  unconditionally. The described bug does not exist. The comments do.
- **Aura: four grains is a thin cloud.** With the longest grains it is dense enough;
  at 70 ms the overlap drops to about 2× and the granular layer becomes audible as
  individual events rather than texture.
- **Aura: four lines is sparse at the largest sizes.** A 436 ms tail from four lines
  is a wash, not a hall — the intent, but SIZE at maximum is not "a bigger room", it
  is "a vaguer one".
- **Aura sweeps rather than crossfades.** Changing SIZE glides the delay reads, so a
  fast SIZE move pitches whatever is in the network; DISTANCE does the same to the
  internal pre-delay, with a 350 ms smoother making it slow rather than clicky.
- **Aura's wet level has not been calibrated against anything**, and at full DISTANCE
  the dry sits around −12.6 dB before the direct trim.
- **Aura's soft ceiling is claimed never to engage.** That follows from the loop-gain
  bound; it is not an observation, and nothing counts how often it fires.
- **Aura's grains do not transpose** — deliberate, but the shimmer this topology is
  one line away from is not available.
- **Shadow's pitch shifter smears transients and combs during the crossfade.** The
  technique, not a bug, but small non-zero PITCH values are the least convincing part
  of the engine even with the crossfade to the unshifted tap.
- **Shadow's wrap rate is computed from the target window, not the smoothed one**, so
  during a PITCH move the increment and the window length disagree slightly for the
  length of the smoother. Small, and unmeasured.
- **Shadow glides LENGTH rather than crossfading it**, so a fast LENGTH move sweeps
  the duplicate's pitch; the 400 ms smoother makes that a feature rather than a click.
- **Shadow is mono-sourced**, so material that is already wide loses its image in the
  duplicate, and at BLUR 0 the duplicate is itself nearly mono — the allpass length
  difference is the only thing decorrelating the channels.
- **Shadow has no tempo sync on LENGTH** (the parameter list has no division, so
  adding one means adding a parameter) and its tank delays are fixed, so BLUR changes
  the density and decay of the smear but not its spacing.
- **Patina's blunting slew limiter is a nonlinearity and is not oversampled.** On the
  420 Hz…2.6 kHz band its low-order products land under Nyquist at 44.1 kHz, but its
  high-order products fold. Off in three of six profiles for that reason. Unmeasured.
- **Patina's noise TYPE snaps on a profile change** rather than crossfading, while
  every other field morphs over about 40 ms. On a bed at −45 dBFS or below that is a
  change of texture rather than a step in level, but it is a snap — and the same
  applies to an Alter morph between profiles whose types differ.
- **Patina's makeup gain assumes one fixed spectrum**, so a patch that is all top end
  will not be level-matched across a profile change.
- **Patina's erosion threshold follows a full-band reference envelope**, so a patch
  with a loud low end erodes its highs slightly more than one without. Arguably
  correct — the surface is one surface — but a choice, not a law.
- **Patina's block-rate filter moves lag the drift by one block.** `lastDrift` is the
  drift value of the last sample of the *previous* block, so the detail ceiling, the
  noise band top and the smear coefficient sit one block behind the drift TONE and the
  noise level use per sample. Inaudible at 0.6 Hz by the author's own arithmetic, but
  it is a lag, and after `reset()` it is zero.
- **Patina keeps running for roughly 70 ms after being switched off**, not the 8 ms
  its own comment implies.
- **`alterAmount` reaches Patina and nothing else here.** Aura and Shadow ignore it,
  so Alter has no effect on the environment or on the duplicate.
