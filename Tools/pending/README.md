# Pending

Work that is finished but not yet applied, parked here only because this
container is ephemeral and the scratchpad it was written in dies with it.

Nothing in this directory is built, and nothing here should stay here.

## `simd-callsite.patch`

The six hunks that switch `SynthVoice` from eight scalar `AnalogOscillator`s
to the vectorised `UnisonOscillatorBank`. **Until it is applied, the
`UnisonOscillatorBank` in `AnalogOscillator.*` is dead code that costs
nothing and buys nothing.**

It was not applied at the time it was written because the 4x-oversampling
work was live in the same file. It therefore needs merging by hand rather
than a clean `git apply`.

Measured before it was parked, on this container only (an Intel Xeon at
2.10 GHz; note this box benchmarks about 1.6x faster than whatever machine
produced the 137% figure in STATUS.md):

| | scalar | vector | |
|---|---|---|---|
| 32 voices x 8x unison, synth | 77.8 % | 68.5 % | 1.14x |
| 32 voices x 8x unison, chain | 87.0 % | 74.4 % | 1.17x |
| oscillator instructions | 14.50 G | 6.33 G | 2.29x fewer |

The output is **bit-identical**: 29 benchmark renders byte for byte, all 300
factory presets identical in every measured column, and `SynthVoice::render`'s
instruction count identical to the digit outside the oscillator.

**The decision this is waiting on.** Either apply it, or delete both this and
the `UnisonOscillatorBank` it calls. A second implementation of the oscillator
that nothing calls is a worse liability than a missing 12%, and it has to stay
in step with the scalar one forever. It buys nothing at low unison, and it
does not bring 32 voices at 8x unison into realtime - it makes the one case
that is over budget less far over.

One figure was never obtained and could change the answer: the plugin target
builds with `-flto` and the benchmark does not, so some of the call overhead
this removes may already be inlined away in the shipping build.
