# The golden set — phases 25 and 26

A golden preset is one the instrument is **judged by**. If the engine changes
and a golden preset moves, something has been broken or improved, and somebody
has to say which.

**That judgement is a listening judgement.** No tool in this repository can
make it, and nothing in this repository claims to have made it. What follows
is the half that can be automated, and the half that cannot, kept clearly
apart.

---

## What is automated

```
NacarBench --golden                      render, fingerprint, compare, report drift
NacarBench --golden --out ./audition     the same, and write the audio out
NacarBench --golden --rewrite            replace the stored fingerprints
```

Twelve presets, chosen to cover the instrument rather than to flatter it: each
of the three characters, each filter model, the tape and digital degradation
paths, the granular and reverb tails, a bass that has to hold its low end and a
drum that has to hit. Seven of them are also **printed and mutated from a fixed
seed**, which makes those entries golden in a stronger sense — a mutation is a
function of the engine alone, so a change to the mutation engine appears here as
a number rather than as an opinion.

Each entry is fingerprinted on peak, RMS, spectral centroid, low and high energy
share, mono retention and length, and compared against
`Tools/golden-baseline.txt`. The tolerances are generous enough to absorb
floating-point reassociation from a compiler change and tight enough that a
revoice cannot hide:

| | drift threshold |
|---|---|
| peak, RMS, mono retention | 0.10 dB |
| spectral centroid | 1 % |
| length | exact |

**Drift is not automatically a defect.** An intended improvement drifts too.
What drift means is that somebody now has to listen and decide.

---

## What is not automated, and cannot be

Accepting a golden preset means sitting down with the rendered audio and
answering, for each one:

1. **Is this the sound the entry claims?** Every entry carries a one-line claim
   — "a tine whose index decays while the filter opens", "an 808 whose pitch
   falls on the attack". If the audio does not demonstrate that, either the
   preset or the claim is wrong.
2. **Would you use it?** A golden preset is a standard, not merely a passing
   measurement. A patch that measures perfectly and that nobody would load is
   not golden.
3. **Does it hold up against the reference?** Play it beside a commercial
   instrument doing the same job. This is the question the whole set exists to
   answer and the one no measurement approaches.

For the mutations, one more:

4. **Is the result musically related to its source?** A mutation that is merely
   different is a random process. The engine's premise is that it breaks a
   history apart and reconstructs it *musically* — a listener has to confirm
   the reconstruction is recognisable as a descendant.

---

## The rule about the baseline

`Tools/golden-baseline.txt` records what the engine does **today**. It is not
an approval and regenerating it is not acceptance.

> **Never run `--rewrite` because the numbers moved.** Run it because somebody
> listened to the new audio and decided it was the sound we want. The file
> exists to make a change visible, and rewriting it to silence a report is the
> one use that defeats it entirely.

---

## Status

Built, and **not accepted**. The harness renders, fingerprints, compares and
writes audio. The twelve entries have never been auditioned — there is no
audio output in this environment and nobody has heard a single one of them.

Phases 25 and 26 are therefore **ready to audition**, not complete, and should
not be recorded as complete until a person has answered the four questions
above.
