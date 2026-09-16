# Screenshots

Captured from the Linux standalone under Xvfb by
`Tools/scripts/screenshot.sh`, at 100 % interface scale.

| | |
|---|---|
| `main.png` | the MAIN page — the locked reference layout |
| `page_mod.png` | MOD — LFOs with live shape previews, three ADSR curves, Breath, Pulse, the mod matrix |
| `page_fx.png` | FX — the six chain modules in DSP order with their full parameter sets |
| `page_seq.png` | SEQ — four lanes of sixteen steps |
| `page_mix.png` | MIX — balance and gain staging |

The instrument is silent in these captures — there is no MIDI input under a
virtual X server — so the viewport reads `NO SIGNAL` and the output meter is
unlit. Both are correct behaviour, not missing features.

These are a record of what the build actually renders. They are not the art
direction: that is `DesignReference/NACAR_V1_LOCKED_ART_DIRECTION.png`, and the
MAIN page is what should be compared against it.
