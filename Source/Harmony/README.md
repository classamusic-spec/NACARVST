# The NÁCAR harmony engine

Specification §162 asks for this document. Phase 20 does two things, and they
are deliberately separate:

```
detectKey()   twelve chroma energies in, a key out - or an honest admission
              that there is no key
Context       the constraint a mutation works inside: given a key and the
              HARMONY control, which pitches are allowed, and where a
              forbidden one goes
```

Phase 21 consults `Context` for every pitch decision it makes, so everything
here is written to be copied freely, called from a loop, and never to allocate.

One rule matters more than the rest of this page: **when the key is not known,
nothing is constrained.** An instrument that transposes a drum loop into E flat
minor because it guessed is broken in a way the user cannot diagnose.

---

## The scales

Nine, in the order the `scale_type` parameter lists them after AUTO. The `Scale`
enum is indexed by that parameter, and a test asserts the two lists still agree
name for name — if somebody adds a scale to one and not the other, the suite
fails rather than the instrument silently playing phrygian when the user asked
for lydian.

| | semitones | mask | COLOR adds |
|---|---|---|---|
| MAJOR | 0 2 4 5 7 9 11 | `0xAB5` | ♯4, ♭7 |
| MINOR (natural) | 0 2 3 5 7 8 10 | `0x5AD` | ♮6, ♭2 |
| DORIAN | 0 2 3 5 7 9 10 | `0x6AD` | ♮3, ♭6 |
| PHRYGIAN | 0 1 3 5 7 8 10 | `0x5AB` | ♮2, ♭5 |
| LYDIAN | 0 2 4 6 7 9 11 | `0xAD5` | ♮4, ♭7 |
| MIXOLYDIAN | 0 2 4 5 7 9 10 | `0x6B5` | ♮7, ♭3 |
| HARMONIC MINOR | 0 2 3 5 7 8 11 | `0x9AD` | ♮6, ♭7 |
| MELODIC MINOR (ascending) | 0 2 3 5 7 9 11 | `0xAAD` | ♭6, ♭7 |
| CHROMATIC | all twelve | `0xFFF` | nothing left to borrow |

Bit 0 is the root and every mask has it. `snapCents` depends on that: it brackets
an arbitrary pitch between the scale tone below and the one above, and the search
is only guaranteed to terminate because the tonic is always a member of its own
scale.

No scale here has a gap wider than three semitones. That is what lets `snap`
promise it never moves a pitch by more than one, and a test checks the gap rather
than trusting the table.

---

## Key detection

### The method

Rotation correlation, the Krumhansl-Schmuckler family. The chroma is centred once
and correlated (Pearson) against **twelve roots × eight scales = 96 candidates**;
the best one wins. Pearson rather than a dot product because chroma arrives in
whatever unit the analyser felt like: correlation is invariant to the scale and
the offset of its input, so a quiet sample and a loud one give the same answer.

Every profile is a permutation of the same twelve numbers, so they share a mean
and a standard deviation, and those are computed once instead of 96 times.

**CHROMATIC is never a detection result.** Its profile would be flat, which has
no variance and therefore no correlation; and its chroma — twelve equal bins — is
exactly the input that means *there is no key here*. Chromatic is a thing a user
chooses, not a thing that is detected. A test asserts that twelve equal tones
return root −1.

### The profiles, and where the numbers came from

The weights are **Temperley's** revision of the Krumhansl-Kessler profiles
(D. Temperley, *The Krumhansl-Schmuckler key-finding algorithm revisited*, Music
Perception 17(1), 1999; and *The Cognition of Basic Musical Structures*, 2001):

```
major   5.0  2.0  3.5  2.0  4.5  4.0  2.0  4.5  2.0  3.5  1.5  4.0
minor   5.0  2.0  3.5  4.5  2.0  4.0  2.0  4.5  3.5  2.0  1.5  4.0
```

Read those two rows **at their own scale degrees** and something useful falls
out. Temperley's major profile, sampled at 0 2 4 5 7 9 11, is

```
tonic 5.0   second 3.5   third 4.5   fourth 4.0   fifth 4.5   sixth 3.5   seventh 4.0
```

and his minor profile, sampled at 0 2 3 5 7 8 11 — which is the **harmonic**
minor scale, not the natural one — is the same seven numbers in the same order.
His two profiles are one degree-weight vector laid on two different scales, with
2.0 for everything chromatic.

So the generalisation to nine scales is not an invention. The rule is his:

1. give each scale degree the weight Temperley gives that degree;
2. give every pitch class outside the scale 2.0.

The one deviation is his 1.5 at the flat seventh, which is an asymmetry his two
profiles share because in both of them the ♭7 is a chromatic tone. In a scale that
*has* a flat seventh it is the seventh degree, not a passing tone, so it takes
4.0 like any other seventh. Every out-of-scale pitch class is 2.0.

The resulting profiles:

```
MAJOR            5.0 2.0 3.5 2.0 4.5 4.0 2.0 4.5 2.0 3.5 2.0 4.0
MINOR            5.0 2.0 3.5 4.5 2.0 4.0 2.0 4.5 3.5 2.0 4.0 2.0
DORIAN           5.0 2.0 3.5 4.5 2.0 4.0 2.0 4.5 2.0 3.5 4.0 2.0
PHRYGIAN         5.0 3.5 2.0 4.5 2.0 4.0 2.0 4.5 3.5 2.0 4.0 2.0
LYDIAN           5.0 2.0 3.5 2.0 4.5 2.0 4.0 4.5 2.0 3.5 2.0 4.0
MIXOLYDIAN       5.0 2.0 3.5 2.0 4.5 4.0 2.0 4.5 2.0 3.5 4.0 2.0
HARMONIC MINOR   5.0 2.0 3.5 4.5 2.0 4.0 2.0 4.5 3.5 2.0 2.0 4.0
MELODIC MINOR    5.0 2.0 3.5 4.5 2.0 4.0 2.0 4.5 2.0 3.5 2.0 4.0
```

Membership does the scale discrimination — natural and harmonic minor differ by
4.0 against 2.0 at two pitch classes — and the degree hierarchy picks the root
out of the seven rotations that share a mask.

**Why not the original Krumhansl-Kessler numbers.** They were tried first. KK
gives the leading tone only 2.88 in major, barely above its own chromatic floor,
and the consequence is measurable: a plain C major seventh chord (C E G B) came
back as **E minor with full confidence**, because E-G-B-C lands on the four
highest weights of the E minor profile. Under the Temperley weights the same
chord is C major. KK's minor profile is also a blend of natural, harmonic and
melodic practice, so it puts nearly equal weight on ♭7 and ♮7 — which makes
natural minor and harmonic minor almost indistinguishable, and this instrument
offers both as separate choices.

### Confidence

Confidence is not decoration and it is not `r`. It is three measurements
multiplied together, and each one exists because of a specific failure:

**Peakedness** — `1 − H(chroma)/ln 12`, one minus the normalised entropy. Zero for
twelve equal bins whatever their level, and near zero for the broad smear a kick,
a snare or a cymbal leaves behind. Ramped from 0.03 to 0.14: below 0.03 there is
no key at any confidence; 0.14 is where real tonal material sits. Without this
term a rotation correlation will happily read a tonal centre out of a noise
floor, because *some* rotation always fits best.

**Fit** — the winning correlation, ramped from 0.45 to 0.80. Measured over 3,000
draws, independent random chroma produces a best-of-96 correlation with a median
of 0.62; synthetic tonal material sits at 0.94 with a 5th percentile of 0.86. The
window is placed between them.

**Lead** — how far the winner is ahead of the best candidate *at a different
root*, over a 0.10 correlation window. This is the term that makes the relative
major/minor case behave. A perfect fit with no lead at all is worth 0.30, so a
genuine tie cannot clear the 0.55 that `AnalysisResult::keyIsUsable()` requires.

`scaleConfidence` is `rootConfidence` multiplied by the same lead measurement
taken over the rival *scales at the winning root*, floored at 0.25. It can never
exceed the root confidence, because a mode without a tonic is not an answer.

### What it does, measured

| input | answer | root conf | scale conf |
|---|---|---|---|
| C E G | C MAJOR | 0.79 | 0.20 |
| A C E | A MINOR | 0.79 | 0.20 |
| C E G B | C MAJOR | 0.81 | 0.20 |
| I–IV–V–I in C | C MAJOR | 1.00 | 1.00 |
| any of the 8 scales, tonic-weighted, any root | itself | ≥ 0.90 | ≥ 0.90 |
| the seven tones of C major, unweighted | C MAJOR | **0.30** | 0.30 |
| a pure C sine (one chroma bin) | C MAJOR | **0.05** | 0.01 |
| twelve equal bins | **no key** | 0.00 | 0.00 |
| near-flat percussive chroma, worst of 2,000 | — | **0.0000** | — |
| independent random chroma | usable 4.1 % of 2,000 | | |

Two of those rows are the point of the whole confidence calculation.

**The unweighted diatonic set scores 0.30**, below the usability threshold, and it
should: the seven tones of C major *are* the seven tones of A minor and of D
dorian. A set of pitch classes with no emphasis has no tonic by construction, and
anything that claimed to find one there would be reporting the order its own loop
happened to run in. The triad, by contrast, is decisive — a C major triad is not
ambiguous with an A minor triad, because they differ by a note.

**A single sine scores 0.05.** One pitch class is not a key. Note the distinction
the return value makes: root −1 means *there is nothing tonal in this chroma at
all*; a root with a low confidence means *here is the best candidate, do not act
on it*. Both are honest, and they are not the same statement.

### What this method is bad at

Bluntly, because plausible output looks like correct output:

- **It cannot hear the difference between a key and a lucky draw.** Twelve
  independent random numbers clear the usability threshold 4.1 % of the time. Any
  rotation-correlation method has this property: a sparse random vector and a
  sparse key-defining vector are the same kind of object. What saves the
  instrument in practice is that real atonal material — percussion — produces a
  *broad, smooth* chroma rather than a sparse random one, and the peakedness term
  catches that: 2,000 near-flat percussive chroma vectors produced a worst
  confidence of 0.0000.
- **It has no time axis.** It is handed one averaged chroma for a whole sample. A
  file that modulates, or a two-bar loop that is C major in the first bar and E
  flat major in the second, is averaged into something that is neither. The right
  fix is per-frame detection with a stability term — the same key winning frame
  after frame — and it is not implemented, because the analysis hands over a
  single vector.
- **It does not know about bass.** Root position and first inversion are the same
  chroma. A human hears C-E-G over a C in the bass as C major without hesitating;
  this sees three pitch classes.
- **Relative and parallel keys are its two standing ambiguities.** The relative
  case is handled — it is reported as low confidence rather than a coin flip — but
  handled is not solved. On genuinely ambiguous material this will return the
  relative key about as often as the right one, with a confidence that says so.
- **Eight scales compete for every answer**, which is seven more than the
  classical algorithm was validated on. Nobody has measured this against a
  labelled corpus; the profiles for the seven added scales follow Temperley's own
  construction rule, but Temperley never published them and no listener rated
  them.
- **The chroma is somebody else's.** Everything above assumes phase 19 hands over
  a chroma that is tuned, folded and weighted sensibly. A chroma whose bins are
  half a semitone out will produce a confident answer a semitone away from the
  truth, and nothing here can tell.

---

## The constraint

`Context` is four fields — root, scale, mode, `keyKnown` — 16 bytes, trivially
copyable, with nothing in it that changes while a mutation runs. `snap`,
`snapCents` and `permits` allocate nothing and branch on a single mask.

| mode | permitted |
|---|---|
| SAFE | the scale |
| COLOR | the scale plus two borrowed tones |
| FREE | all twelve |

### The sharp edge: SAFE with no key

**Under SAFE with `keyKnown == false`, nothing is constrained.** `snap` is the
identity, `snapCents` returns zero, `permits` returns true for everything. So is
COLOR. This is the honest reading of "stay in the detected key" when there is no
detected key, and it is asserted explicitly by a test over all nine scales, all
twelve roots and all 128 semitones.

`Context::from` decides `keyKnown`, and it holds a line: **a key is a root and a
mode.** The root can only come from the analysis, and only if
`AnalysisResult::keyIsUsable()` agrees — that threshold lives in `AnalysisResult`
so that "do we know the key?" has one answer everywhere in the instrument. The
mode can come from the analysis or from the user forcing `scale_type`. Forcing a
scale over an unusable root does **not** make a key: nothing in a scale choice
supplies a tonic. A confident root with no scale is not a key either — it is half
of one, and half a key silently snapping thirds is exactly the failure this phase
exists to prevent.

`forcedScale` is the `scale_type` parameter **minus one**, because index 0 of
that list is AUTO. Negative means AUTO. Get that wrong at the call site and MAJOR
becomes MINOR.

### COLOR — the one interpretive decision here

COLOR is **parallel modal interchange, one step in each direction**: the scale
plus the two tones that the modes immediately either side of it on the brightness
continuum have and it does not. The continuum is the familiar one, each step
flattening exactly one degree:

```
lydian — ionian — mixolydian — dorian — aeolian — phrygian — locrian
```

So MAJOR borrows the ♯4 above it and the ♭7 below it; MINOR borrows dorian's ♮6
and phrygian's ♭2; and so on down the table at the top of this page. Harmonic and
melodic minor are not diatonic modes and do not sit on that line, so they take the
equivalent step by practice instead of by rotation: they borrow each other's sixth
and seventh, which is literally how the minor scale is used — one form ascending,
another descending.

Why this rule rather than a list somebody liked:

- **Every borrowed tone is a semitone neighbour of a degree the scale already
  has.** It arrives as a chromatic alteration of a known degree, not as a foreign
  note. That is what "stays consonant with it" has to mean in a system that has to
  work for nine scales without a human checking each one.
- **The tonic and the fifth are never borrowed against.** No interchange moves
  them, so the key keeps its floor and its ceiling however far COLOR wanders.
- **The third is doubled, never replaced.** DORIAN gains the natural third and
  MIXOLYDIAN the flat one — the blues third, in both directions — and in both
  cases the original third stays permitted. Nothing COLOR does can turn a minor
  key major.
- **It is the same size everywhere.** Nine of twelve pitch classes for every
  seven-note scale. SAFE is 7, COLOR is 9, FREE is 12, and the control has three
  positions that are actually different. A rule that gave one scale eight tones
  and another eleven would make the middle position mean something different
  depending on the sample, which is not a control at all.

The tones it will not borrow are as much the decision as the ones it will. In
minor, COLOR admits ♭2 and ♮6 but not the major third and not the tritone; in
major, ♯4 and ♭7 but not the minor third, the ♭2 or the ♭6. If a mutation needs
those, that is what FREE is for, and the user asked for it.

### snap

Nearest permitted semitone; an input already in the scale is returned untouched.
Ties resolve **downward** — snapping never raises a pitch the caller did not ask
to raise. Because no scale here has a gap wider than an augmented second, `snap`
never moves a pitch by more than **one semitone**, and the test asserts exactly
that rather than a loose bound. It is idempotent by construction: its output is in
the mask, and anything in the mask is returned unchanged.

It does not clamp to the MIDI range. Snapping 0 or 127 can step one semitone
outside it, because the alternative is returning a pitch that is not in the scale
and quietly breaking the postcondition everything else relies on. Range belongs to
the caller.

### snapCents

`snapCents` is **not** `snap` in finer units, and the difference is the reason it
exists. A hard quantiser cannot be continuous: crossing the middle of a gap moves
the target by the whole gap, so a glide through it jumps — by 100 cents in a
semitone gap, 200 in a whole tone, and 300 at the augmented second in harmonic
minor. You cannot mix that out.

So it is a warped map instead. Inside each gap between neighbouring scale tones:

```
first quarter     pinned to the lower tone
middle half       smootherstep, 6u⁵ − 15u⁴ + 10u³
last quarter      pinned to the upper tone
```

which is continuous, has a continuous slope, is monotonic, and lands **exactly**
on a scale tone for half of every gap. Measured over a 0.5-cent sweep across
every scale at every root:

| | |
|---|---|
| largest step in the output for a 0.5-cent step in | **1.88 cents** |
| largest correction it ever applies | **89.5 cents** |
| correction at a pitch that is already a scale tone | **0.000 cents** |
| backward movement anywhere (monotonicity) | **none** |

The largest correction is 0.2984 of the gap — the extremum of
`smootherstep(u) − t`, not the quarter the dead zone suggests — so 29.8 cents
inside a semitone, 59.7 inside a whole tone, and 89.5 at the augmented second.

The cost lives in the middle of the gap: a glide crosses it at up to **3.75 times**
the rate it went in. That is the trade a continuous quantiser has to make
somewhere, and it is made away from the scale tones, where the ear is listening,
rather than at them.

One consequence worth knowing: **a pitch exactly half way between two scale tones
does not move at all.** Moving it would mean choosing a direction, and that choice
is precisely what puts the jump back in the curve. `snap` does choose — it has to,
it returns an integer — so `snap(6)` in C major is 5 while `snapCents(600)` is 0.
They are different functions with different contracts, not an inconsistency: on
anything `snap` has already returned, `snapCents` agrees it is home, and a test
checks that for all 128 semitones in every scale at every root.

---

## What the tests prove

`NacarTests` / **Harmony**, sixteen cases:

- the `Scale` enum still matches the `scale_type` choice list, name for name;
- every mask contains its own root, has seven tones (twelve for chromatic), is
  distinct from the other eight, and has no gap wider than three semitones;
- a clean major triad is C major with confidence 0.79 — and reports a *low* scale
  confidence, because three notes do not fix the mode;
- a clean minor triad is A minor, while the unweighted diatonic set it shares with
  C major is reported as **not usable**;
- a flat chroma at any level, and an all-zero chroma, return root −1 and
  confidence exactly 0;
- all eight non-chromatic scales are detected back from their own tones at all
  twelve roots, with a weakest confidence of 0.900;
- detection is transposition invariant: shifting a chroma by N shifts the root by
  N and changes neither the scale nor either confidence by more than 1e-4;
- 2,000 percussion-like chroma vectors produce a worst confidence of 0.0000, and
  fewer than one random chroma in ten is read as a key (measured: 4.1 %);
- `Context::from` carries a confident analysis, lets a forced scale win, and
  refuses to report a key from a weak root, a missing root, or a missing scale;
- `permits` agrees with `maskOf` for every scale, every root and every semitone;
- SAFE snaps every one of 128 chromatic inputs into the mask, moves nothing that
  was already in it, never moves anything further than a semitone, and is
  idempotent;
- FREE is the identity for all 128 semitones in every scale at every root;
- **SAFE and COLOR with `keyKnown == false` are the identity** — snap, snapCents
  and permits all — and a `Context` built from an empty `AnalysisResult` is too;
- COLOR is a strict superset of SAFE with exactly nine tones, and snapping under
  it lands inside its own set;
- `snapCents` never jumps, never goes backwards, never moves a scale tone, and
  never applies more than 89.5 cents.

---

## Known limitations

- **Nobody has listened to this.** No sample has been mutated with it, because
  phase 21 does not exist yet. Everything above is a measurement or a proof about
  a function, and neither is a judgement about whether a mutation *sounds* in key.
- **The detector has never seen a real chroma.** Every test input is synthetic,
  with a known answer by construction. That is the right way to test the maths and
  it is worth more than eyeballing one file, but it means the accuracy figure that
  matters — how often this is right about actual audio — is unknown. There is no
  labelled corpus in this repository and no harness to run one.
- **4.1 % of random chroma vectors are read as a usable key.** See *What this
  method is bad at*: this is a property of rotation correlation, not a bug that
  was left in, and the confidence is the only defence.
- **One chroma for a whole sample.** No modulation, no key changes, no per-frame
  stability term. A sample that moves between keys gets an average of them.
- **The profiles for seven of the nine scales are constructed, not measured.**
  They follow Temperley's own placement rule, which is a good reason to believe
  them and is not evidence.
- **`root_note` has no route into `Context::from`.** The parameter exists — AUTO
  plus twelve names — and the frozen signature of `Context::from` takes only the
  forced *scale*. `Context` is a plain struct, so phase 21 can set `root` and
  `keyKnown` itself after building one; that is the intended workaround and it is
  not done here, because doing it would mean editing a frozen header.
- **Nothing consumes any of this yet.** No caller builds a `Context`, and
  `detectKey` is called only by the tests and by the phase 19 analyser. Until
  phase 21 uses it, the HARMONY control still does nothing audible.
- **`scaleConfidence` is computed and returned but never acted on.** `keyIsUsable`
  gates on the root confidence alone. A high-confidence root with a coin-flip
  scale — a bare triad, exactly the case in the table above — currently produces a
  fully constrained SAFE context built on a mode nobody is sure of. The honest fix
  is a second threshold, and it belongs with whoever decides what a mutation does
  when it half knows the key.
