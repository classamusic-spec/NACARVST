# MEMORY

> SYNTH creates a powerful present. MEMORY creates a past.

Memory is not an effect that happens to age things. It is the answer to one
question — **how many times has this been copied, and how long ago?** —
expressed in twelve dimensions at once.

Specification §95 keeps the three ageing systems apart and this engine respects
the line:

| | owns |
|---|---|
| **MEMORY** | historical identity — generational loss |
| RETRO | the medium and the playback machine — tape, its wow, its dropouts |
| PATINA | surface texture and age |

So there is no transport in here, no flutter, no dust. There is a chain of
copies, and everything in the engine is a consequence of that.

---

## The generation model

`memory_gen` selects I, II, III or IV. **Generation N runs N copies in series.**

That is the whole design decision, and it is not the obvious one. The obvious
implementation is one stage with a depth control, and it is wrong, because
copying is a *cascade*: each pass band-limits a little, softens a transient a
little, saturates a little, smears phase a little, drifts a little and leaves a
little noise behind — and the next pass does the same thing to the result,
**including to the artefacts the previous pass added**. Generation III's noise
floor has itself been resampled twice. That compounding is the entire difference
between a third-generation copy and a first-generation copy turned up, and it is
what "sample of a sample" means.

`GenerationModels.h/.cpp` describes what **one** copy does. `MemoryEngine` runs
N of them. The four rows of the character table are not four intensities of one
thing — each says what the *nth* pass through the medium did, and they get
progressively less forgiving:

| | | |
|---|---|---|
| **I** | subtle recorded character | 13 kHz ceiling, 1.15× resampling, 3 cents of drift, −69 dBFS floor |
| **II** | noticeable resampling | 8.5 kHz, 2.45× with the anti-alias filter half open, 5 cents |
| **III** | sample-of-a-sample | 5.2 kHz, 3.7× mostly aliased, 7 cents, **and** dropouts and a spectral hole appear |
| **IV** | deep artefact | 3.3 kHz, 5.4× effectively unfiltered, 9 cents, hard dropouts, a deep hole, near-hold reconstruction |

Dropouts and spectral holes exist only in rows III and IV. They are not a
turned-up version of anything in rows I and II — generation genuinely changes
internal processing behaviour, as §70 asks.

`macro_memory` and `memory_gen` are independent and both matter: the macro is
how strongly each copy is applied, the generation is how many there are.

---

## The twelve dimensions

§69 forbids reducing Memory to noise plus wow. All twelve are implemented, and
the notes below say what each one actually does rather than what it is called.

| # | dimension | what it does |
|---|---|---|
| 1 | **Bandwidth** | Per copy: a shared low cut, and two cascaded poles off the top whose corner is interpolated *geometrically* from 20 kHz toward the copy's ceiling. The corner is never still — a slow oscillator plus Breath move it, widened by Motion, because the medium's bandwidth was not a constant either. |
| 2 | **Transient softness** | A fast peak follower against a slow one; the ratio between them is the attack. Gain comes down instantly and returns over 25 ms, so the attack rounds off while the sustain keeps its brightness. A low pass would have dulled both. The detector is the mono sum and the gain is applied to both channels, so it can never move the image. |
| 3 | **Saturation** | Asymmetric soft clip with a per-copy bias whose *sign alternates* between generations, so the harmonic signature builds up and partly cancels rather than simply accumulating. Unity small-signal gain by construction — the make-up is the curve's own slope at the bias point, so moving the bias changes the harmonic mix without changing the level — then a DC blocker. |
| 4 | **Resampling** | The defining sound from generation II on. One decimation clock, shared by both channels; the reconstruction is a blend of linear interpolation and zero-order hold. The artefacts are therefore *reconstruction* error — images and rolloff — not quantisation, which is what separates this from Crush. The anti-alias filter ahead of the decimator opens progressively with generation, so generation IV folds its own top back down over itself. The divisor scales with sample rate: a third-generation copy decimates to the same absolute rate at 96 kHz as at 48 kHz. |
| 5 | **Pitch instability** | Two components. The *common* drift is a fractional delay read that is identical on both channels — two summed incommensurate oscillators plus Breath, seeded per copy from a deterministic RNG, so a session recalls identically. The *differential* drift is a second, much smaller per-channel delay: two copies never drifted together. |
| 6 | **Phase diffusion** | Three short Schroeder allpasses (7, 13, 23 samples at 48 kHz) with **identical coefficients on both channels**. That is deliberate: one LTI filter applied to both channels cannot change the L/R correlation at any frequency, so the most "it has been through something" process in the engine is also the one that carries no stereo risk at all. |
| 7 | **Stereo coherence** | A pair of short allpasses per channel with the **same delays and opposite coefficient signs**. Same delays matters: an allpass with a zero coefficient is a plain delay, so giving the channels different delays would offset them by several samples even with decorrelation at zero. Opposite signs give the two channels the most different phase response available while both stay exactly magnitude-flat. |
| 8 | **Harmonic colouration** | Chebyshev-shaped second and third harmonic terms on the body band only, with per-generation weights. Normalised by that band's own envelope, so the harmonic-to-fundamental *ratio* stays roughly constant with level — a signature rather than another saturator, which is what distinguishes it from dimension 3. The squared term has a non-zero mean for anything but a sine, so it gets its own DC blocker. |
| 9 | **Spectral aging** | `fx::Tilt`, darkening with generation. Level-neutral by construction. |
| 10 | **HF absorption** | A second low pass whose corner is pulled down by the signal's own envelope, so the medium absorbs more when it is driven harder. Distinct from dimension 1, which is static bandwidth; this one only exists while something loud is happening. |
| 11 | **Channel asymmetry** | `memory_asymmetry` leans the HF corner, the saturation drive and bias, the absorption depth, the noise level, the decorrelation and the differential drift. Each copy leans in the direction given by the pattern `+ − − +`, whose partial sums never exceed one, so however many copies run the image does not accumulate a pull. All of it is static within a block: audible as character, never as an unstable image. |
| 12 | **Noise interaction** | Not a bed. Correlated and per-channel components, coloured by generation (hiss at I, rumble at IV) with sample-rate-correct normalisation; **ducked** by the signal, **lifted in the gaps**, and gated by a 2.5 s activity envelope so it survives a musical rest but not a genuinely silent passage. It is injected *early* in each copy, so the first copy's noise is resampled, band-limited and copied again by every copy after it. |

### Signal order inside one copy

```
common pitch drift  ->  transient softener  ->  low cut  ->  2 x high cut
  ->  HF absorption  ->  asymmetric saturation  ->  DC  ->  noise injection
  ->  decimate + imperfect reconstruction  ->  3 x common phase diffusion
  ->  [split: harmonic colouration on the body band]
  ->  differential drift  ->  2 x decorrelation allpass
  ->  channel asymmetry x dropout  ->  spectral hole  ->  tilt
  ->  mid/side, side high passed at 165 Hz  ->  guard
```

---

## The low end

§38, §40 and §43 are emphatic, and the synth already collapses everything below
about 130 Hz to mono. Memory must not undo that.

The guarantee is **arithmetic, not architectural**. Every copy ends by taking
its own mid/side and high passing the side with two poles at 165 Hz — above the
synth's own 130 Hz, so the protected band is strictly wider than the band the
source made mono. Whatever the copy did per channel, the low end comes out
correlated. The high pass crossfades in with the amount of decorrelation
actually in use, so a Memory that is not decorrelating does not impose mono on a
source that legitimately has stereo bass.

The first version tried to do this with a band split instead — decorrelate the
mid and high bands only and leave the low band alone. It failed in a way worth
recording: the decorrelation allpasses delay the mid path and the low path is
not delayed with them, so the two bands recombined out of phase and put a
**+2.1 dB bump at 300 Hz** into the output. Measured, not suspected. Doing the
per-channel work full band and fixing the side afterwards has no crossover to
misalign, and the side high pass takes a further 18 dB off whatever reaches
60 Hz.

---

## Macro response

There is no central routing table, on purpose. This is Memory's:

| macro | what it adds |
|---|---|
| `macro_memory` | the depth of everything. Zero is an exact early-out. |
| `memory_gen` | how many copies. Structural, not a depth. |
| `macros.age` | deepens the *character* of each copy rather than its depth: more saturation, more bandwidth loss, more colouration, a darker tilt, a louder floor. |
| `macros.movement` | widens and speeds up the two slow instabilities — the pitch drift and the bandwidth wander. A still patch has a stable copy; an alive one has a wandering one. |
| `macros.pulseToMemory × pulseAt(i)` | momentarily deepens the artefacts: saturation drive, decimation, reconstruction error, transient softening and noise, for as long as the pulse lasts (§83). |
| `macros.alterAmount` | blends the whole character table toward the alternate lineage — a sampler chain instead of a tape chain: brighter, harder decimation, more aliasing, a more quantised reconstruction, a brighter floor. The amount of damage is unchanged; its shape is not. |
| `macros.breathAt(i)` | rides the slow parameters only — the drift, the bandwidth wander and the spectral hole. Never the per-sample ones. Its depth is scaled by Motion. |

Each of these *adds* to the four Memory controls rather than replacing them, so
a patch that sets `memory_bandwidth` explicitly still wins.

**Deliberately not consumed:** `macros.grit` (Character already reaches Memory
through `age`; taking both would count Character twice), `macros.scale`,
`distance`, `wetBias`, `widthScale` (World's business, and Memory must not be a
second width control), `lfo1`/`lfo2` (the mod matrix routes those explicitly;
Memory's own drift is not an LFO destination), and the transport fields (nothing
in generational loss is tempo-synchronous). `macros.memory` and
`macros.memoryGeneration` duplicate the registry values, and the registry is
read instead.

---

## Gain staging

§148: do not use loudness to fake quality.

A chain of band limiters and soft clippers only ever removes energy, so the
correction is deliberately **lopsided: up to +6 dB of make-up, never more than
−3 dB of cut**. It is measured per chunk, applied with one chunk of lag, and
moves with a 250 ms time constant, so it restores an average and never a
transient — it cannot become a compressor, and it cannot invent the loud
direction. Below a silence floor it holds rather than trying to normalise the
engine's own noise.

---

## Bypass, transitions and latency

**Bypass is exact.** At `macro_memory` 0 the block returns before touching the
buffer. Not "the chain at zero depth" — the chain at zero depth still contains
delay lines.

**Engaging Memory, or changing generation, changes how much delay the chain
contains**, so neither can be done by switching. Both restart the copies from
silence behind a 12 ms crossfade from the dry signal: audibly a re-print, which
is the honest thing for a control that says "this is now a different number of
copies".

**Latency is real and Memory does not compensate it itself.** One copy is
3.07 ms at every supported sample rate; generation IV is **12.3 ms**.
`getLatencySamples()` reports what is currently in force — zero while the macro
is at zero — in the same shape the FX slots use, so the chain can add it to its
own total; `latencySamples(generation)` answers the same question for a
generation that is not running. A source-to-master latency budget is not a
decision one engine should be making by itself. Most of it is the pitch drift's delay
line, and it is a direct trade: `kWobbleBaseSeconds` (0.0015) buys drift depth
with latency, one for one.

---

## Measurements

From a standalone harness driving `CopyStage` directly at 44.1 / 48 / 88.2 /
96 kHz, block sizes 32 / 37 / 100 / 255 / 512 / 1024, all four generations and
five parameter sets from all-minimum to all-maximum. The harness is not checked
in — nothing outside this directory was created — but it is a hundred lines and
`NacarBench` should absorb it.

**Low-band L/R correlation, worst case over every combination above: 0.998.**
No non-finite sample anywhere in the sweep. Peak output 0.90 for an input
peaking near 1.4.

Stereo coherence above 2 kHz for a mono input — 1.000 would mean no
decorrelation at all:

| | gen I | gen II | gen III | gen IV |
|---|---|---|---|---|
| default controls | 0.84 | 0.88 | 0.86 | 0.80 |
| everything at maximum | 0.37 | 0.09 | 0.01 | −0.06 |

Pitch drift, peak deviation of a 1 kHz sine tracked by zero crossings:

| | gen I | gen II | gen III | gen IV |
|---|---|---|---|---|
| `memory_wobble` 0.35 | 1.2 ¢ | 2.4 ¢ | 3.7 ¢ | 5.7 ¢ |
| `memory_wobble` 1.0 | 2.7 ¢ | 6.2 ¢ | 10.3 ¢ | 14.6 ¢ |

Single copy, generation I, `macro_memory` 0.22, `memory_bandwidth` 0.5,
`memory_diffusion` 0.3, wobble and asymmetry at zero so a sine measurement is
not smeared by drift, all macros zero. The response is flat to ±0.2 dB through
the midrange and the loss is where it is supposed to be:

| 40 | 100 | 300 | 1 k | 4 k | 8 k | 12 k |
|---|---|---|---|---|---|---|
| −0.7 | −0.2 | −0.1 | −0.2 | −1.0 | −3.5 | −8.0 dB |

Broadband level change **before** the engine's make-up, by material:

| | gen I | gen II | gen III | gen IV |
|---|---|---|---|---|
| bass-dominated, `macro_memory` 0.22 | −0.9 | −1.6 | −2.4 | −3.1 dB |
| mid pad, 0.22 | −0.4 | −0.7 | −1.0 | −1.5 dB |
| mid pad, everything at maximum | −1.0 | −2.3 | −3.4 | −5.2 dB |
| near-white, everything at maximum | −4.1 | −7.3 | −10.5 | −14.6 dB |

The +6 dB of make-up covers everything musical. It does not cover the last row,
and it is not supposed to — see the limitations.

CPU, 48 kHz, 256-sample blocks, one cloud VM core, all controls at 0.5, treat
the ratios as more meaningful than the absolutes:

| gen I | gen II | gen III | gen IV |
|---|---|---|---|
| 1.5 % | 2.9 % | 4.3 % | 5.7 % |

---

## Known limitations

- **Nobody has listened to this.** Everything above is measured. Whether it
  sounds like a past is not a measurable property and this document will not
  pretend otherwise.
- **12.3 ms of uncompensated latency at generation IV**, and it *changes with
  the generation*, so a host that tracks plugin latency will see it move at
  runtime. Nothing reports it yet. This is the single biggest thing wrong with
  the engine.
- **Very bright material at maximum settings still ends up 8 dB down** after the
  make-up. Four copies genuinely remove that much top, and widening the make-up
  further would mean boosting a signal that has almost nothing left above 4 kHz.
  A band-aware match would fix it properly and is not implemented.
- **Changing generation dumps the tail.** All copies are reset and crossfaded
  from dry over 12 ms. Preserving the tail would need two chains and a real
  crossfade between them.
- **Turning `macro_memory` to exactly zero from a deep setting steps.** The
  falling edge cannot fade, because a fade would mean the bypassed block is not
  bit-identical to its input, and exact bypass was judged the more important of
  the two. Engaging fades in correctly.
- **The dimension I am least happy with is 8, harmonic colouration.** The
  envelope normalisation makes the harmonic ratio level-independent, which is
  what "colouration rather than saturation" needs, but the normalised term is
  hard-clamped at ±2 to stop a transient against a small envelope from cubing
  into an explosion — so on percussive material the colouration momentarily
  becomes a clipper instead of a colour. It is quiet enough that this is subtle,
  and it is still the weakest idea in the file. A fixed-gain waveshaper driven
  by a slow band-level estimate would be more honest.
- **The pitch drift is delay-depth-limited at slow rates.** The deviation a
  given delay depth produces is proportional to the drift rate, so a very slow
  copy drifts less than its nominal cents figure. The clamp is silent; the
  character table's `wobbleCents` is therefore an upper bound, not a promise.
- **Dropouts and the spectral hole are untested by ear and barely tested by
  measurement.** The sweep proves they do not produce non-finite output or break
  the low-end rule. It does not prove the rate is musical; the roll happens
  about twice a second at full depth and that number is a guess.
- **The saturation hard-clips above roughly 1.25 full scale** at maximum drive,
  because `fx::tanhFast` clamps its input at ±3. The synth peaks near −6 dBFS so
  this should not be reachable in the instrument, but a hot source into
  generation IV will find it.
- **No SIMD, and the per-sample loop branches per channel.** The two channels
  are processed by the same code with an inner loop over `cIdx`, which is clear
  to read and leaves roughly a factor of two on the table.
- **Mono buffers are processed as a stereo pair and the right channel is
  discarded**, which costs twice the CPU it needs to. Buffers with more than two
  channels leave channels 3 and up untouched.
- **`memory_gen` and `macro_memory` are read from the registry, not from
  `MacroState`.** The registry's `raw()` now includes the modulation overlay, so
  this picks up anything the mod matrix is doing to either; what it would miss
  is a change made only inside the macro resolver. The two should not disagree,
  and if they ever do, this is where to look.
- **The mono path here is dead code in practice.** `NacarEngine` already renders
  everything in stereo and folds at the end, so the mono mirror in this engine
  only fires if Memory is driven directly.
