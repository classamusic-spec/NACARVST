# NÁCAR V1 — LOCKED ART DIRECTION

**Visual source of truth:** `DesignReference/NACAR_V1_LOCKED_ART_DIRECTION.png`

This document is a written transcription of that image. It exists so that layout
work never depends on eyeballing a screenshot. Every number below is expressed in
**logical units on a 1536 × 1024 canvas** and is mirrored, one-for-one, by the
constants in `Source/UI/Layout.h`.

> The image is the source of truth. This file is the transcription.
> If they ever disagree, the image wins and this file gets corrected.

---

## 0. Canvas

| | |
|---|---|
| Logical size | 1536 × 1024 |
| Aspect ratio | 3 : 2 |
| Scale steps | 75 % · 100 % · 125 % · 150 % · 200 % |
| Outer margin | 8 |
| Panel corner radius | 14 (large panels) · 10 (cards) · 8 (pills) |

The window resizes by uniform scale only. Nothing reflows: NÁCAR is a machined
front panel, not a responsive web page.

---

## 1. Material language

### Ceramic / machined aluminium (the chassis)

```
#E6E2DE   ceramic light      top of every bevel, panel fill top
#D8D3CF   ceramic mid        panel fill bottom
#C8C3BF   ceramic dark       recesses, knob shadow seats
#B4AEA9   ceramic edge       hairline borders
```

### Optical glass (viewport, FX chain, nav bar, preset bar)

```
#0C0C0E   glass deep         centre of the viewport
#111114   glass mid          card fills
#17171A   glass raised       card tops, hover
#2A2A30   glass edge         hairline borders inside glass
```

### Accents

```
#B8A0FF   violet primary     active state, waveform, selection
#C9B7FF   violet light       highlights, glow core
#A987FF   violet deep        rims, pressed
#9DFFE4   mint primary       activity / power-on indicator
#78F5D5   mint deep          meter body
```

### Type

```
#2B2825   ink                primary label on ceramic
#6E6862   ink muted          secondary label on ceramic
#9A938C   ink faint          scale ends, units
#F2F0EE   glass ink          primary label on glass
#8A8A93   glass ink muted    secondary label on glass
```

### Shading vocabulary

Every raised ceramic element carries, in order:

1. a **contact shadow** below (2 px offset, 8 px blur, 18 % black),
2. a **specular top edge** (1 px, 60 % white),
3. a **bevel bottom edge** (1 px, 22 % black),
4. a very faint **radial sheen** from the upper-left.

Every recessed glass element carries an **inner shadow** at the top and a
1 px `glass edge` hairline. Glass never has a drop shadow — it is a cut-out.

---

## 2. Global regions

| Region | x | y | w | h |
|---|---|---|---|---|
| Header bar | 8 | 10 | 1520 | 86 |
| Left macro panel | 8 | 102 | 317 | 818 |
| Optical viewport | 333 | 102 | 875 | 337 |
| Mutate panel | 333 | 447 | 875 | 267 |
| FX chain | 333 | 722 | 875 | 198 |
| Right atmosphere panel | 1216 | 102 | 312 | 818 |
| Bottom bar | 8 | 928 | 1520 | 66 |
| Footer type | 8 | 994 | 1520 | 22 |

---

## 3. Header

| Element | Position | Notes |
|---|---|---|
| `NÁCAR` wordmark | x 66, baseline y 60 | 30 pt, letter-spacing 0.34 em, ink |
| `MEMORY INSTRUMENT` | x 68, baseline y 78 | 8 pt, letter-spacing 0.42 em, ink muted |
| Vertical divider | x 302, y 30 → 76 | 1 px, ceramic edge |
| `SOUNDS` / `WITH A PAST.` | x 322, baselines y 51 / 65 | 8.5 pt, 0.16 em, ink muted |
| Preset bar (glass) | 493, 30, 608 × 54 | radius 12 |
| ‹› source glyph | centre 531, 57 | glass ink muted |
| ⦀ meter glyph | centre 581, 57 | three vertical bars, violet |
| Preset name | x 608, baseline y 64 | 17 pt, glass ink |
| Prev / next arrows | centres 838 / 890, y 57 | chevrons |
| Favourite (heart) | centre 958, 57 · r 20 | ceramic disc, raised **on** the glass strip |
| `BROWSER` pill | 990, 39, 88 × 36 | ceramic pill **on** the glass strip, 9 pt, 0.16 em |
| Settings gear | centre 1245, 53 | ink muted |
| Wave logo ∿ | centre 1343, 53 | three overlapping arcs, ink |
| Coordinates | x 1410, baselines y 50 / 64 | `25.7617° N` / `80.1918° W`, 8.5 pt |

---

## 4. Left macro panel

All knobs: ceramic body, machined concentric sheen, a single dark indicator line
from centre outward, and a violet value arc drawn in the **seat groove** just
outside the body.

| Control | Centre | Body radius | Label baseline |
|---|---|---|---|
| MEMORY | 143, 208 | 82 | 312 |
| CHARACTER | 94, 415 | 48 | 476 |
| MOTION | 236, 415 | 48 | 476 |
| WORLD | 94, 600 | 48 | 669 |
| WEIGHT | 236, 600 | 48 | 665 |
| ALTER | 113, 820 | 70 | 899 |
| RANDOM | 259, 815 | 22 | 856 |

RANDOM is the only **dark** knob in the panel — a small machined black cap with a
violet indicator. It is visually subordinate on purpose: it is not MUTATE.

### Generation selector

Four dots at x 253, y 168 / 202 / 236 / 269, labels `I II III IV` at x 272.
Inactive: 5 px ceramic-dark dot. Active: 6 px violet dot with an 11 px glow halo.

### Scale legends

| Under | Text | Baseline |
|---|---|---|
| CHARACTER | `CLEAN` … `WORN` | 500 |
| MOTION | `STILL` … `ALIVE` | 500 |
| WORLD | `INTIMATE` … `EXPANSIVE` | 694 |

7.5 pt, 0.12 em, ink faint, ends aligned to the knob's horizontal extent with a
hairline rule between them.

### WEIGHT mode selector

Segmented `SUB | BODY | AIR` at 190, 676, 96 × 22, radius 6. Selected segment is
`ceramic dark` with ink text; unselected segments are flush with the panel.

---

## 5. Optical viewport

Glass panel 333, 102, 875 × 337. Inner content inset by 19.

| Element | Rect / position |
|---|---|
| Source accent bar | 357, 136, 2 × 34 (violet) |
| `User Sample` | x 393, baseline 150 |
| `guitar_loop.wav  ·  44.1 kHz  ·  2:13` | x 393, baseline 169 |
| Rename pencil | centre 604, 163 |
| `SNAP` pill | 946, 137, 57 × 29 |
| Waveform / list / markers / expand | centres 1032 / 1071 / 1114 / 1158, y 151 · 29 × 29 |
| Waveform field | 352, 186, 840 × 160 |
| Overview strip | 352, 348, 840 × 20 |
| Transport row | y 380 → 424 |

The **waveform-mode** icon is the active one in the reference: violet fill,
violet ink. The other three are glass-raised with muted ink.

### Waveform field

- Violet waveform, mirrored about the vertical centre, drawn as a filled envelope
  with a brighter 1 px crest.
- **Selection**: translucent violet rectangle between x 678 and x 889, with a
  vertical handle line at each edge running the full field height and a 5 px
  filled circle handle at the top (y 194).
- **Playhead**: 1 px near-white line at x 611.
- Overview strip repeats the same envelope at low contrast with a lighter
  "visible window" box.

### Transport

| Control | Centre |
|---|---|
| Play (filled circle) | 386, 401 · r 22 |
| Stop (rounded square) | 444, 401 · r 22 |
| Reset / return-to-zero | 505, 401 |
| `0:34.2 / 2:13` | x 549, baseline 406 |
| Loop | 717, 401 |
| Trim to selection | 769, 401 |
| Shuffle | 847, 401 |
| Zoom out | 897, 401 |
| `ZOOM` label | x 960, baseline 405 |
| Zoom slider | 1003 → 1130, y 401 · handle at 1075 |
| Zoom in | 1158, 401 |

> **Correction.** This table said *Stop (filled circle)* until it was checked
> against the approved image, where stop is a rounded square — which is also
> why it reads as a sibling of the four square view-mode buttons above it
> rather than a second play. Play is the only disc in the transport row. The
> transcription was wrong, not the image; where the two disagree the image
> wins, and this file is the thing that gets corrected.

---

## 6. Mutate panel

Ceramic panel 333, 447, 875 × 267.

| Element | Rect / position |
|---|---|
| Sparkle mark | centre 368, 481 |
| `MUTATE` | x 393, baseline 492 · 25 pt, 0.06 em |
| `TRANSFORM SOUND INTELLIGENTLY` | x 394, baseline 508 · 7.5 pt, 0.2 em |
| `HARMONY` label | x 700, baseline 486 |
| Harmony segmented | 765, 466, 175 × 31 — `SAFE` `COLOR` `FREE` |
| `DISTANCE` label | x 970, baseline 486 |
| Distance segmented | 1030, 466, 160 × 31 — `NEAR` `FAR` `UNKNOWN` |

Harmony selection is **violet-filled**; Distance selection is **dark-filled**.
That asymmetry is deliberate and is in the reference — harmony is the musical
decision, distance is the structural one.

### Intent row (y 530, h 32, radius 8)

`MEMORY` `CLOUD` `BROKEN` `REVERSE` `DISTANT` `RHYTHMIC` `DARK` `GHOST`
`PLAYABLE` `CINEMATIC` — laid out left to right starting at x 357 with an 8 px
gap, each pill sized to its label + 34 px padding. 8.5 pt, 0.1 em.

### Action row (y 590, h 57)

| Button | Rect | Style |
|---|---|---|
| `MUTATE` | 355, 590, 183 × 57 | violet-tinted ceramic, violet rim, sparkle icon |
| `AGAIN` | 541, 590, 186 × 57 | glass, cube icon |
| `PRINT` | 730, 590, 160 × 57 | glass, page icon |
| divider | x 925, y 598 → 640 | 1 px ceramic edge |
| `MAKE INSTRUMENT` | 955, 590, 235 × 57 | ceramic, waveform icon + right chevron |

### Preserve row (baseline 692)

`PRESERVE` label at x 357. Then seven labelled switches and one unlabelled
master switch:

`PITCH` `KEY` `RHYTHM` `TRANSIENTS` `STEREO` `LENGTH` `LOW END` + master

Switch: 26 × 14 track, radius 7, ceramic-dark when off with the knob left;
violet when on with the knob right. Label 8 pt, 0.1 em, to the right of the
track.

---

## 7. FX chain

Glass panel 333, 722, 875 × 198.

| Element | Position |
|---|---|
| `FX CHAIN` | x 349, baseline 747 |
| Add `+` | 428, 733, 22 × 22 |
| Collapse | 1172, 733, 24 × 22 |
| Card row | y 777, h 100 |

Six cards — `RETRO` `CRUSH` `FILTER` `REWIND` `GRAIN` `SPACE` — each 112 wide on
a 132 px pitch starting at x 350, then a **dashed add-slot** 55 wide at x 1140.

Card anatomy (top to bottom):

- `−` bypass glyph, top-left, 8 px in
- name, centred, 8.5 pt, 0.14 em, glass ink
- `×` remove glyph, top-right
- a large **module glyph** in the middle (cassette, dot-matrix, filter curve,
  ⏪, dot-matrix, concentric circle)
- a **mint power ring**, bottom-right, lit when active

A small **chain link** icon sits between adjacent cards at y 826, on the seam.
Cards are drag-reorderable and the displayed order *is* the DSP order.

In the reference, `RETRO` is the selected card (violet rim + violet glyph);
the rest are unselected with lit mint power rings.

---

## 8. Right atmosphere panel

Ceramic panel 1216, 102, 312 × 818, divided into four modules by hairlines at
y 332, 561 and 755.

Each module has the same anatomy:

```
[icon]  NAME                                    (power)
        SUBTITLE
                      [ big knob ]        • PARAM
                                            PARAM
                                            PARAM
                                            PARAM
```

| Module | Icon centre | Title baseline | Subtitle baseline | Knob centre | Knob r | Power centre |
|---|---|---|---|---|---|---|
| AURA | 1265, 146 | 144 | 158 | 1322, 252 | 70 | 1486, 146 |
| SHADOW | 1265, 373 | 371 | 385 | 1322, 479 | 70 | 1486, 373 |
| BREATH | 1265, 597 | 595 | 609 | 1322, 679 | 62 | 1486, 597 |
| PATINA | 1265, 788 | 786 | 800 | 1322, 861 | 62 | 1486, 788 |

Subtitles: `SPACE & ENVIRONMENT`, `ATMOSPHERIC DUPLICATE`, `ORGANIC MOVEMENT`,
`TEXTURE & AGE`.

Parameter lists sit at x 1440 with a 1 px vertical rule at x 1429 spanning the
list, and a violet dot at x 1414 marking the **selected** parameter — the one the
module's big knob is currently driving.

| Module | Parameters (baselines) |
|---|---|
| AURA | SIZE 200 · DISTANCE 227 · FOG 253 · DECAY 279 · LIGHT 304 |
| SHADOW | LENGTH 431 · DISTANCE 457 · BLUR 482 · PITCH 508 · LEVEL 533 |
| BREATH | AMOUNT 653 · SPEED 678 · RANDOM 704 · SHAPE 730 |
| PATINA | TONE 838 · NOISE 860 · WEAR 881 · DRIFT 903 |

Module icons: AURA = ringed planet; SHADOW = **a pill toggle switch**, not an
icon (it is the module's on/off in the reference); BREATH = a three-line wave;
PATINA = an outlined triangle.

Power buttons are ceramic circles r 13 with a violet power glyph.

---

## 9. Bottom bar

### Source selector (left)

Pills at y 940, h 40, radius 10, each with a leading icon:

| Pill | x | w | Icon |
|---|---|---|---|
| `SYNTH` | 35 | 89 | small waveform |
| `SAMPLE` | 135 | 96 | waveform (active — **dark glass**, violet ink) |
| `GRAIN` | 242 | 95 | dotted circle |
| `RESONATOR` | 346 | 109 | tuning fork / arc |
| `SPECTRAL` | 465 | 101 | triangle |

### Mode navigation (centre)

Glass bar 590, 930, 490 × 60, radius 14. Five items, each an icon above a
7.5 pt / 0.14 em label:

`MAIN` (active — ceramic pill 605, 936, 90 × 48) · `MOD` · `FX` · `SEQ` · `MIX`
at centres 650 / 747 / 840 / 929 / 1020.

### Output (right)

`OUTPUT` label x 1135, baseline 963. Meter 1205, 952, 195 × 12 — a **segmented
mint gradient** (deep → primary) with a darker track. Master knob centre
1472, 957 · r 27, ceramic with a violet indicator.

### Footer type

`A PAST LIVES IN EVERY SOUND` at x 35, baseline 1002 — 7 pt, 0.24 em, ink faint.
`NÁCAR   V1.0.0   MMXXV` right-aligned to x 1500, same style.

---

## 10. Interaction contract

| Gesture | Result |
|---|---|
| Drag knob vertically | change value (1 px ≈ 0.4 % of range) |
| Shift + drag | fine (÷ 6) |
| Double click | reset to default |
| Ctrl / Cmd + click | type a value |
| Right click | context menu · MIDI learn |
| Mouse wheel over knob | ± one step |
| Hover 700 ms | tooltip: name · value · one-line explanation |

Knobs never rotate past ±140° from top. Bipolar knobs draw their arc from the
12 o'clock position outward in both directions.

---

## 11. Things that must not change

The following are locked for V1 and are not open to redesign:

- the three-column chassis and the position of every region in §2
- the macro hierarchy MEMORY → CHARACTER / MOTION → WORLD / WEIGHT → ALTER
- the Memory generation selector as four Roman numerals
- HARMONY (SAFE · COLOR · FREE) and DISTANCE (NEAR · FAR · UNKNOWN) as two
  independent segmented controls
- the ten intent buttons and their order
- MUTATE · AGAIN · PRINT · MAKE INSTRUMENT as the four actions
- the seven preserve locks
- the six FX modules and the fact that display order equals DSP order
- AURA · SHADOW · BREATH · PATINA as the four right-hand modules
- the five source modes and the five bottom nav pages
- violet = active/selected, mint = powered/alive. Nothing else uses those hues.

---

## 12. How the reference is lit

Section 1 lists the shading vocabulary. This section says how the approved
image actually applies it, region by region, because that is the part a
coordinate table cannot carry — and because the image itself is not currently
in the repository (see `README.md`). It was written by reading the approved
image directly.

### The rule underneath all of it

**One light, upper-left, about sixty degrees of elevation, and it never moves.**
Expressed once in code as `theme::lightX` / `theme::lightY`, and every
specular, bevel and contact shadow in the instrument derives from it. A panel
of controls reads as machined metal only while they all agree about where the
light is; one element lit from its own direction breaks the illusion for
everything around it.

Two consequences that are easy to get backwards:

- **A raised object** is brightest on its upper-left edge and casts its shadow
  down and to the right.
- **A recess** is the inverse: darkest on the edge *nearest* the light, because
  that is the wall the light cannot reach. A dark rectangle drawn on ceramic is
  not a recess. An inner shadow under its top edge is.

### Knobs

Light ceramic caps, near-white at the upper-left falling to a warm grey at the
lower-right — a point light inside the disc, not a linear ramp. Each cap sits
in a visible darker seat ring, darkest on the upper-left side. The rim catches
a bright thin highlight along the upper-left arc and loses it entirely on the
lower-right. One thin dark indicator line runs from about 45 % of the radius to
the edge. The contact shadow falls down and to the right. The violet value arc
sits in a groove *outside* the cap, about 4 px wide, and glows into that groove
rather than sitting on top of it. The small RANDOM knob is the dark variant:
near-black cap, violet indicator.

### Buttons and pills

Corner radius about 8. Just off the surface: a thin bright top edge, a thin
dark bottom edge, a small contact shadow, and — the detail that most separates
a moulded object from a rectangle with a border — an inset highlight one pixel
inside the top edge, which reads as the material's own thickness. A selected
pill is brighter *and* sits slightly lower: pressed in, not merely tinted.

MUTATE is the only element in the interface that emits light: a pale lilac fill
lit from within, a violet border, and a small halo on the ceramic around it.
AGAIN and PRINT are near-black glass — a dark control on a dark ground carries
no information in its face, so all of it is in the edges.

### Segmented controls and toggles

A recessed track with the selected segment raised inside it. The contrast
between the two is the whole effect, and it fails if the track is merely a
darker fill. Toggles are the same at small scale: dark recessed track, light
raised thumb with its own contact shadow.

### The glass regions

The viewport, the FX rack, the preset bar and the bottom nav slab are all
cut-outs, not objects: inner shadow under the top edge, a 1 px `glass edge`
hairline, and **no drop shadow**. The viewport is deeper at its centre than at
its edges. Inside the rack, the cards are a shade lighter than the rack itself,
which is what makes them read as seated *in* it.

### What emits

Only two things, and they must both genuinely glow rather than being drawn in a
bright colour:

- **Violet** — the active FX card's border and icon, the waveform, the value
  arcs, the active generation dot, the parameter dots on the atmosphere
  modules, MUTATE.
- **Mint** — the power rings and the output meter, which reads as light inside
  a dark channel.

Everything else is lit; nothing else is a source.
