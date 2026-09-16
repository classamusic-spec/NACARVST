#include "PatinaEngine.h"

#include "../DspCommon.h"
#include "../Sources/Synth/VoiceModules.h"     // NoiseExciter, DriftGenerator

#include <array>
#include <cmath>

/*
    =======================================================================
    PATINA                                          specification section 95
    =======================================================================

    WHAT THIS IS

    Patina is the finish on the object.  Memory is how many times the sound was
    copied; Retro is which machine played it back; Patina is the surface it has
    ended up with.  That distinction is the whole design brief, and it decides
    what is NOT in this file:

      - no dropouts, no wow, no flutter, no pitch instability of any kind.
        Timing damage is the medium's business, which is Retro's.
      - no generation loss, no bandwidth collapse.  That is Memory's.
      - nothing deep or specific.  Patina touches everything a little: it is
        thin and broad, and it is the difference between two patches that are
        otherwise identical.

    THE CHAIN, per channel, in order

        1  four-band split          140 Hz / 420 Hz / 2.6 kHz
        2  static surface colour    one gain per band, from the profile
        3  WEAR                     detail ceiling, erosion, blunting
        4  recombine + HAZE smear   two allpasses, phase only
        5  TONE                     fx::Tilt, pivoting at 700 Hz
        6  NOISE                    profile-coloured, ducked, excited, moving
        7  band-gain makeup, guard, engage crossfade against the dry input

    The split is fx::ThreeBand with fx::TwoBand inside its mid band.  Both are
    complementary TPT one-poles, so low + lowMid + upperMid + high reconstructs
    the input exactly: with every profile gain at 1 and every control at 0 the
    stage is transparent by construction rather than by trimming.

    WEAR IS EROSION OF DETAIL, NOT DAMAGE.  Three mechanisms, all confined to
    the top half of the spectrum:

      detail ceiling  a one-pole lowpass on the high band whose corner falls
                      from the profile's ceilingHz to its ceilingFloorHz.  The
                      surface can no longer hold the finest detail.
      erosion         downward expansion of the high band.  Gain is
                      (env + f.thr) / (env + thr), so loud high-frequency
                      material passes and low-level detail is worn away - which
                      is what a worn surface does.  The threshold is a fraction
                      of a slow envelope of the programme, not an absolute
                      level, so the effect does not change when the patch gets
                      louder.  The floor f bounds the attenuation at -9 dB, so
                      it can never become a gate.
      blunting        a slew-rate limit on the 420 Hz - 2.6 kHz band.  The
                      limit is proportional to the same reference envelope, so
                      it rounds the fastest edges at any level.  Profiles that
                      should stay clean set bluntRate to zero.

    NOISE IS NOT A BED.  A fixed hiss added to the output would be the laziest
    thing this module could do, so the noise here is built out of four things
    that all move:

      colour     each profile names one of the synth core's NoiseExciter
                 types (pink, white, texture) and then band-limits it.
      ducking    a fast envelope of the programme pulls the noise down under
                 loud passages: gain = 1 - duck * env/(env + knee), with an
                 absolute knee, because a real noise floor is a fixed level
                 that loud material masks.
      excitement the opposite term.  A proportion of the noise is driven BY the
                 material rather than hidden by it - grain that lives on the
                 surface and appears when the surface is struck.  CHROME is
                 mostly excited, VINTAGE mostly ducked.
      drift      the noise level, and the top of its band, move with the
                 surface drift.  SMOKE is almost entirely this.

    STEREO AND THE LOW END (specification 38, 40, 43).  Nothing in here is
    allowed to decorrelate the bass:

      - the noise is generated as one correlated mono core plus a per-channel
        side component, and the side is high-passed by two poles at 900 Hz, so
        below 900 Hz the noise is identical in both channels.
      - the mono core's own band starts at 300 Hz at the lowest (SMOKE).
      - the HAZE smear is a pair of allpasses with identical coefficients in
        both channels.  It smears phase without widening, because widening the
        mid band costs mono retention.
      - the drift modulates filters, never a delay.  Nothing here changes the
        arrival time of anything.
      - the low band is never eroded, never blunted, never smeared.

    DRIFT.  synth::DriftGenerator - two sines at a near-golden ratio, evaluated
    at control rate and interpolated.  A bounded random walk is the textbook
    answer but a clamped walk spends its life on the rails; two incommensurate
    sines are bounded by construction and never repeat.  It is blended with
    macros.breathAt(i) in proportion to Motion, so an alive patch drifts
    organically rather than periodically.  Drift moves: the tilt, the detail
    ceiling, the noise level, the noise band top, and the smear coefficient.

    LEVEL.  Patina is a texture stage and has no mix control, so it compensates
    its own band gains: makeup = 1 / sqrt(sum of w[i] * gain[i]^2) over an
    assumed power distribution (30 / 32 / 26 / 12 percent, low to high).  The
    assumption is fixed and is wrong for any individual patch; it is there so
    that switching profile is a change of colour and not a change of level.
    TONE needs no compensation - fx::Tilt pivots, so it trades one end of the
    spectrum against the other.

    MACRO RESPONSE  (added to the controls, never replacing them)

        macros.age        -> WEAR   + 0.35   Memory and Character together are
                                             how used-up the object looks.
        macros.grit       -> NOISE  + 0.30   Character alone is the texture.
        macros.movement   -> DRIFT  + 0.30   Motion is what may wander, and it
                                             also raises the drift rate by up
                                             to 80 % and blends up to 35 % of
                                             Breath into the drift.
        macros.alterAmount-> morphs the surface up to 60 % of the way towards
                                             an alternate profile (SOFT->HAZE,
                                             VINTAGE->SMOKE, CHROME->VINTAGE,
                                             HAZE->SMOKE, SMOKE->VINTAGE).
                                             CUSTOM has no alternate: it is the
                                             one the user is driving.

    BYPASS.  With patina_on false and the engage fade already at zero, process()
    returns before touching the buffer - the output is the input sample for
    sample.  Switching Patina on fades the wet in over about 8 ms; switching it
    off fades out over the same 8 ms and only then starts returning early, so
    neither edge clicks and the steady off state is still bit-exact.

    KNOWN LIMITATIONS - read these before believing anything above.

      - Nobody has listened to this.  Every claim here is a claim about the
        algorithm, not about how it sounds.
      - The blunting slew limiter is a nonlinearity and is not oversampled.  It
        runs on the 420 Hz - 2.6 kHz band, so its low-order products land under
        Nyquist at 44.1 kHz, but its high-order products fold.  It is off in
        three of the six profiles for that reason and never exceeds a rounding
        of the fastest edges.  Unmeasured.
      - The makeup gain assumes one fixed spectrum.  A patch that is all top
        end will not be level-matched across a profile change.
      - The noise TYPE snaps when the profile changes rather than
        crossfading; everything else about a profile morphs over ~40 ms.  A
        type change on a bed that sits at -45 dBFS or below is a change of
        texture, not a step in level, but it is a snap.
      - The erosion threshold follows a full-band reference envelope, so a
        patch with a loud low end erodes its highs slightly more than one
        without.  That is arguably correct - the surface is one surface - but
        it is a choice, not a law.
      - alterAmount morphs between profile coefficients.  Two profiles whose
        noise types differ morph every scalar but jump the type, as above.
*/

namespace nacar
{
    using namespace fx;

    namespace
    {
        constexpr float kEps = 1.0e-9f;

        /** Absolute knee for the noise ducking, about -34 dBFS.  Absolute on
            purpose: a noise floor is a fixed level that loud material masks. */
        constexpr float kDuckKnee = 0.02f;

        /** Erosion threshold as a fraction of the slow programme envelope, and
            the gain floor that stops the expander becoming a gate. */
        constexpr float kErosionRefScale = 0.5f;
        constexpr float kErosionFloor    = 0.35f;

        /** Band split corners.  Fixed, not per profile: the profiles differ in
            what they do to the bands, not in where the bands are. */
        constexpr float kLowHz     = 140.0f;
        constexpr float kLowMidHz  = 420.0f;
        constexpr float kHighHz    = 2600.0f;

        /** Assumed power distribution across the four bands, used only by the
            makeup gain.  Low, low mid, upper mid, high. */
        constexpr float kBandPower[4] = { 0.30f, 0.32f, 0.26f, 0.12f };

        // ===================================================================
        //  Small local helpers
        //
        //  Everything shared lives in DspCommon.  These three are specific to
        //  this engine: a block-rate smoother (the same pattern SynthEngine.cpp
        //  uses privately), an asymmetric envelope follower, and a resonant
        //  band, which DspCommon has no equivalent of.
        // ===================================================================

        /** Block-rate exponential smoothing, evaluated linearly inside a block.

            The registry is read once per block.  Handing that value straight to
            the audio path would step it at every block boundary, which is
            audible on a tilt, a gain or a noise level. */
        struct Smoothed
        {
            float current = 0.0f;
            bool  primed  = false;
            Ramp  ramp;

            void set (float target, int numSamples, float coef) noexcept
            {
                if (! primed)
                {
                    current = target;       // first block: jump, do not glide up
                    primed = true;
                }

                const float next = target + coef * (current - target);
                ramp.set (current, next, numSamples);
                current = next;
            }

            forcedinline float at (int i) const noexcept { return ramp.at (i); }
            float value() const noexcept { return current; }

            /** Pins the smoother at a value while staying primed, so the next
                block glides from here instead of jumping. */
            void hold (float v) noexcept { current = v; primed = true; ramp.snap (v); }

            void reset() noexcept { current = 0.0f; primed = false; ramp.snap (0.0f); }
        };

        /** Asymmetric envelope follower: fast up, slow down. */
        struct Follower
        {
            float z = 0.0f, aUp = 0.0f, aDown = 0.0f;

            static float coefficient (float seconds, double sampleRate) noexcept
            {
                const double s = juce::jmax (1.0e-5, (double) seconds);
                return (float) std::exp (-1.0 / (s * juce::jmax (1.0, sampleRate)));
            }

            void prepare (float upSeconds, float downSeconds, double sampleRate) noexcept
            {
                aUp   = coefficient (upSeconds, sampleRate);
                aDown = coefficient (downSeconds, sampleRate);
                z = 0.0f;
            }

            void reset() noexcept { z = 0.0f; }

            forcedinline float process (float rectified) noexcept
            {
                const float a = (rectified > z) ? aUp : aDown;
                z = rectified + a * (z - rectified);
                return (z = flush (z));
            }
        };

        /** Two-pole TPT state variable, bandpass output only.

            DspCommon has one-poles, complementary splits and a tilt but no
            resonant band, and CHROME's metallic noise needs one.  It is kept
            here rather than added to DspCommon because no other engine has
            asked for it yet; if a second one does, this is the one to move. */
        struct Resonator
        {
            float g = 0.1f, k = 1.0f, a1 = 0.5f, a2 = 0.05f, a3 = 0.005f;
            float ic1 = 0.0f, ic2 = 0.0f;

            void set (float hz, float q, double sampleRate) noexcept
            {
                const double sr = juce::jmax (1.0, sampleRate);
                g = tanPrewarp ((float) (juce::jlimit (20.0, sr * 0.45, (double) hz) / sr));
                k = 1.0f / juce::jlimit (0.5f, 20.0f, q);

                // 1 + g(g + k) with g > 0 and k > 0: never zero, never small.
                a1 = 1.0f / (1.0f + g * (g + k));
                a2 = g * a1;
                a3 = g * a2;
            }

            void reset() noexcept { ic1 = ic2 = 0.0f; }

            /** Bandpass.  Peak gain is 1/k, so callers scale by k for unity. */
            forcedinline float process (float x) noexcept
            {
                const float v3 = x - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;

                ic1 = flush (2.0f * v1 - ic1);
                ic2 = flush (2.0f * v2 - ic2);

                return v1;
            }
        };

        // ===================================================================
        //  THE SIX SURFACES
        //
        //  A profile is not six intensities of one effect.  Each of these is a
        //  different physical finish, and the fields below are what makes them
        //  different rather than merely more.
        // ===================================================================

        /** Every field of a surface, declared once.  The struct, the morph
            towards a new profile and the blend towards an alternate profile are
            all generated from this list, so a new field cannot be left out of
            one of them. */
        #define NACAR_SURFACE_FIELDS(X)                                        \
            X (toneBias)       /* added to TONE                             */ \
            X (noiseScale)     /* multiplies NOISE                          */ \
            X (wearScale)      /* multiplies WEAR                           */ \
            X (driftScale)     /* multiplies DRIFT                          */ \
                                                                               \
            X (lowGain)        /* below 140 Hz                              */ \
            X (lowMidGain)     /* 140 Hz .. 420 Hz                          */ \
            X (upperMidGain)   /* 420 Hz .. 2.6 kHz                         */ \
            X (highGain)       /* above 2.6 kHz                             */ \
                                                                               \
            X (ceilingHz)      /* detail ceiling at WEAR = 0                */ \
            X (ceilingFloorHz) /* detail ceiling at WEAR = 1                */ \
            X (erosionDepth)   /* how far low-level detail is worn away     */ \
            X (bluntRate)      /* slew limit, reference units per second    */ \
                                                                               \
            X (noiseGain)      /* amplitude at NOISE = 1                    */ \
            X (noiseLowHz)     /* mono core band, lower corner              */ \
            X (noiseHighHz)    /* mono core band, upper corner              */ \
            X (noiseRing)      /* 0..1 metallic resonance in the noise      */ \
            X (noiseRingHz)                                                    \
            X (noiseRingQ)                                                     \
            X (noiseSpread)    /* how much decorrelated side noise          */ \
            X (noiseDuck)      /* how far it hides under loud material      */ \
            X (noiseExcite)    /* how far it is instead driven by it        */ \
            X (noiseMove)      /* how far DRIFT moves it                    */ \
                                                                               \
            X (smear)          /* allpass diffusion depth                   */ \
            X (driftRateHz)                                                    \
            X (driftToTone)                                                    \
            X (driftToCeiling) /* octaves                                   */

        struct Surface
        {
            #define NACAR_SURFACE_DECLARE(name) float name = 0.0f;
            NACAR_SURFACE_FIELDS (NACAR_SURFACE_DECLARE)
            #undef NACAR_SURFACE_DECLARE

            /** One-pole towards another surface, at block rate.  This is what
                makes patina_profile click-free: the coefficients morph rather
                than the audio crossfading. */
            void approach (const Surface& target, float coef) noexcept
            {
                #define NACAR_SURFACE_APPROACH(name) name = target.name + coef * (name - target.name);
                NACAR_SURFACE_FIELDS (NACAR_SURFACE_APPROACH)
                #undef NACAR_SURFACE_APPROACH
            }
        };

        inline Surface blendSurface (const Surface& a, const Surface& b, float t) noexcept
        {
            Surface s;
            #define NACAR_SURFACE_BLEND(name) s.name = lerp (a.name, b.name, t);
            NACAR_SURFACE_FIELDS (NACAR_SURFACE_BLEND)
            #undef NACAR_SURFACE_BLEND
            return s;
        }

        // -- SOFT ------------------------------------------------------------
        //  Handled, not aged.  The finish on something that has been picked up
        //  a thousand times: corners rounded, nothing broken, almost silent.
        static const Surface kSoftSurface = []
        {
            Surface s;
            s.toneBias       = -0.10f;
            s.noiseScale     =  0.35f;   // "almost no noise"
            s.wearScale      =  0.75f;
            s.driftScale     =  0.60f;

            s.lowGain        =  1.00f;
            s.lowMidGain     =  1.03f;
            s.upperMidGain   =  1.00f;
            s.highGain       =  0.94f;   // gentle, broad high-frequency loss

            s.ceilingHz      = 16000.0f;
            s.ceilingFloorHz =  6500.0f;
            s.erosionDepth   =  0.25f;
            s.bluntRate      =  0.0f;    // SOFT never acquires an edge

            s.noiseGain      =  0.055f;  // pink, narrow band: about -52 dBFS at NOISE = 1
            s.noiseLowHz     =  900.0f;
            s.noiseHighHz    = 5000.0f;
            s.noiseRing      =  0.0f;
            s.noiseRingHz    = 4000.0f;
            s.noiseRingQ     =  2.0f;
            s.noiseSpread    =  0.50f;
            s.noiseDuck      =  0.80f;
            s.noiseExcite    =  0.15f;
            s.noiseMove      =  0.20f;

            s.smear          =  0.10f;
            s.driftRateHz    =  0.06f;
            s.driftToTone    =  0.05f;
            s.driftToCeiling =  0.12f;
            return s;
        }();

        // -- VINTAGE ---------------------------------------------------------
        //  Thirty years in a case.  Warm, low-mid forward, with the fine even
        //  hiss of a medium that was always slightly noisy.
        static const Surface kVintageSurface = []
        {
            Surface s;
            s.toneBias       = -0.22f;
            s.noiseScale     =  1.00f;
            s.wearScale      =  1.00f;
            s.driftScale     =  0.80f;

            s.lowGain        =  1.00f;
            s.lowMidGain     =  1.10f;   // the warmth, and where it lives
            s.upperMidGain   =  0.98f;
            s.highGain       =  0.88f;

            s.ceilingHz      = 14000.0f;
            s.ceilingFloorHz =  5000.0f;
            s.erosionDepth   =  0.45f;
            s.bluntRate      = 22000.0f; // rounds edges above roughly 3.5 kHz

            s.noiseGain      =  0.075f;  // pink, wide band: about -38 dBFS at NOISE = 1
            s.noiseLowHz     =  500.0f;
            s.noiseHighHz    = 9000.0f;
            s.noiseRing      =  0.0f;
            s.noiseRingHz    = 4000.0f;
            s.noiseRingQ     =  2.0f;
            s.noiseSpread    =  0.35f;   // even, close, not wide
            s.noiseDuck      =  0.60f;
            s.noiseExcite    =  0.25f;
            s.noiseMove      =  0.25f;

            s.smear          =  0.06f;
            s.driftRateHz    =  0.09f;
            s.driftToTone    =  0.08f;
            s.driftToCeiling =  0.18f;
            return s;
        }();

        // -- CHROME ----------------------------------------------------------
        //  A hard plated surface.  Bright, glassy, and it does not go dull with
        //  age - it gets scratched.  So WEAR here blunts edges and adds a
        //  metallic ring rather than closing the top down, and the noise is
        //  mostly excited by the material instead of hiding under it.
        static const Surface kChromeSurface = []
        {
            Surface s;
            s.toneBias       =  0.22f;
            s.noiseScale     =  0.80f;
            s.wearScale      =  0.90f;
            s.driftScale     =  0.70f;

            s.lowGain        =  0.97f;
            s.lowMidGain     =  0.93f;   // hard, not warm
            s.upperMidGain   =  1.04f;
            s.highGain       =  1.12f;   // the glassy top

            s.ceilingHz      = 19000.0f;
            s.ceilingFloorHz = 12000.0f; // stays bright at full WEAR
            s.erosionDepth   =  0.20f;
            s.bluntRate      = 16000.0f; // scratches: the edges go, the top stays

            s.noiseGain      =  0.030f;  // white is hotter than pink, so less of it
            s.noiseLowHz     = 2500.0f;
            s.noiseHighHz    = 14000.0f;
            s.noiseRing      =  0.70f;   // the metallic edge
            s.noiseRingHz    = 7200.0f;
            s.noiseRingQ     =  4.5f;
            s.noiseSpread    =  0.55f;
            s.noiseDuck      =  0.35f;
            s.noiseExcite    =  0.65f;   // it appears when the surface is struck
            s.noiseMove      =  0.15f;

            s.smear          =  0.0f;    // chrome is not diffuse
            s.driftRateHz    =  0.13f;
            s.driftToTone    =  0.06f;
            s.driftToCeiling =  0.08f;
            return s;
        }();

        // -- HAZE ------------------------------------------------------------
        //  A surface you cannot quite see through.  The veil is phase, not
        //  filtering: two allpasses smear the arrival of everything above
        //  140 Hz without removing any of it, and the noise is the widest of
        //  the six.  This is the only profile where WEAR reads as diffusion.
        static const Surface kHazeSurface = []
        {
            Surface s;
            s.toneBias       = -0.05f;
            s.noiseScale     =  0.70f;
            s.wearScale      =  0.80f;
            s.driftScale     =  1.00f;

            s.lowGain        =  1.00f;
            s.lowMidGain     =  1.02f;
            s.upperMidGain   =  0.99f;
            s.highGain       =  0.97f;   // barely filtered: the veil is phase

            s.ceilingHz      = 15000.0f;
            s.ceilingFloorHz =  9000.0f;
            s.erosionDepth   =  0.30f;
            s.bluntRate      =  0.0f;

            s.noiseGain      =  0.050f;
            s.noiseLowHz     =  700.0f;
            s.noiseHighHz    = 11000.0f;
            s.noiseRing      =  0.0f;
            s.noiseRingHz    = 4000.0f;
            s.noiseRingQ     =  2.0f;
            s.noiseSpread    =  0.80f;   // diffuse
            s.noiseDuck      =  0.55f;
            s.noiseExcite    =  0.20f;
            s.noiseMove      =  0.35f;

            s.smear          =  0.85f;
            s.driftRateHz    =  0.05f;
            s.driftToTone    =  0.04f;
            s.driftToCeiling =  0.10f;
            return s;
        }();

        // -- SMOKE -----------------------------------------------------------
        //  Residue that keeps moving.  Dark and irregular: the darkest of the
        //  six, the fastest drift, the most eroded, and a noise floor that is
        //  band-passed by a sweeping filter and then moved bodily by the drift.
        static const Surface kSmokeSurface = []
        {
            Surface s;
            s.toneBias       = -0.35f;
            s.noiseScale     =  1.00f;
            s.wearScale      =  1.10f;
            s.driftScale     =  1.20f;

            s.lowGain        =  1.02f;
            s.lowMidGain     =  1.05f;
            s.upperMidGain   =  0.95f;
            s.highGain       =  0.80f;

            s.ceilingHz      = 11000.0f;
            s.ceilingFloorHz =  3500.0f;
            s.erosionDepth   =  0.65f;   // the most eroded of the six
            s.bluntRate      = 13000.0f;

            s.noiseGain      =  0.045f;  // TEXTURE already carries its own gain
            s.noiseLowHz     =  300.0f;  // the lowest any profile goes, see below
            s.noiseHighHz    = 6000.0f;
            s.noiseRing      =  0.15f;
            s.noiseRingHz    = 1800.0f;
            s.noiseRingQ     =  1.5f;
            s.noiseSpread    =  0.45f;
            s.noiseDuck      =  0.45f;
            s.noiseExcite    =  0.30f;
            s.noiseMove      =  0.95f;   // the floor moves, which is the point

            s.smear          =  0.25f;
            s.driftRateHz    =  0.17f;
            s.driftToTone    =  0.16f;
            s.driftToCeiling =  0.40f;
            return s;
        }();

        // -- CUSTOM ----------------------------------------------------------
        //  No profile bias at all: every band gain is 1, every control bias is
        //  0, every scale is 1.  The four controls and nothing else.  It is
        //  also the only profile with no alternate identity under Alter, since
        //  the point of CUSTOM is that the user is driving.
        static const Surface kCustomSurface = []
        {
            Surface s;
            s.toneBias       =  0.0f;
            s.noiseScale     =  1.0f;
            s.wearScale      =  1.0f;
            s.driftScale     =  1.0f;

            s.lowGain        =  1.0f;
            s.lowMidGain     =  1.0f;
            s.upperMidGain   =  1.0f;
            s.highGain       =  1.0f;

            s.ceilingHz      = 18000.0f;
            s.ceilingFloorHz =  5500.0f;
            s.erosionDepth   =  0.40f;
            s.bluntRate      =  0.0f;

            s.noiseGain      =  0.065f;
            s.noiseLowHz     =  600.0f;
            s.noiseHighHz    = 10000.0f;
            s.noiseRing      =  0.0f;
            s.noiseRingHz    = 4000.0f;
            s.noiseRingQ     =  2.0f;
            s.noiseSpread    =  0.50f;
            s.noiseDuck      =  0.60f;
            s.noiseExcite    =  0.25f;
            s.noiseMove      =  0.30f;

            s.smear          =  0.0f;
            s.driftRateHz    =  0.10f;
            s.driftToTone    =  0.08f;
            s.driftToCeiling =  0.20f;
            return s;
        }();

        constexpr int kNumProfiles = 6;

        inline const Surface& surfaceFor (int profile) noexcept
        {
            switch (profile)
            {
                case 0:  return kSoftSurface;
                case 2:  return kChromeSurface;
                case 3:  return kHazeSurface;
                case 4:  return kSmokeSurface;
                case 5:  return kCustomSurface;
                default: return kVintageSurface;
            }
        }

        /** Which NoiseExciter colour each surface is made of.  PINK for the
            three quiet surfaces, WHITE for CHROME (so the ring has something
            flat to sit on), TEXTURE for SMOKE - a band-passed noise whose
            centre already sweeps on its own. */
        inline int noiseTypeFor (int profile) noexcept
        {
            switch (profile)
            {
                case 2:  return 0;   // WHITE
                case 4:  return 6;   // TEXTURE
                default: return 1;   // PINK
            }
        }

        /** Where Alter takes each surface.  CUSTOM stays where it is. */
        constexpr int kAlternateProfile[kNumProfiles] = { 3, 4, 1, 4, 1, 5 };

        /** Block-rate one-pole coefficient for a time constant in seconds. */
        inline float blockCoefficient (float seconds, int numSamples, double sampleRate) noexcept
        {
            const double blockSeconds = (double) juce::jmax (1, numSamples)
                                      / juce::jmax (1.0, sampleRate);
            return (float) std::exp (-blockSeconds / juce::jmax (1.0e-4, (double) seconds));
        }
    }

    // =======================================================================
    //  Impl
    // =======================================================================
    struct PatinaEngine::Impl
    {
        // -- geometry -------------------------------------------------------
        double sampleRate = 48000.0;

        // -- per channel ----------------------------------------------------
        struct Channel
        {
            ThreeBand  bands;          // 140 Hz / 2.6 kHz
            TwoBand    lowMidSplit;    // 420 Hz, inside the mid band
            OnePoleTPT ceiling;        // the detail ceiling WEAR closes
            Follower   hfEnv;          // envelope of the high band
            Tilt       tone;           // TONE, pivoting at 700 Hz
            Allpass    smearA, smearB; // HAZE, phase only, identical both sides
            float      slew = 0.0f;    // blunting state

            synth::NoiseExciter sideNoise;
            OnePoleTPT sideHpA, sideHpB, sideLp;

            void prepare (double sr, juce::uint32 seed, int maxDelaySamples)
            {
                bands.prepare (kLowHz, kHighHz, sr);
                lowMidSplit.prepare (kLowMidHz, sr);
                ceiling.setCutoff (16000.0f, sr);
                hfEnv.prepare (0.004f, 0.050f, sr);
                tone.prepare (sr, 700.0f);

                smearA.prepare (maxDelaySamples);
                smearB.prepare (maxDelaySamples);
                smearA.setDelay (juce::jmax (2.0f, (float) (sr * 0.0009)));
                smearB.setDelay (juce::jmax (2.0f, (float) (sr * 0.0023)));

                sideNoise.prepare (sr, seed);
                sideHpA.setCutoff (900.0f, sr);
                sideHpB.setCutoff (900.0f, sr);
                sideLp.setCutoff (9000.0f, sr);

                reset();
            }

            void reset() noexcept
            {
                bands.reset();
                lowMidSplit.reset();
                ceiling.reset();
                hfEnv.reset();
                tone.reset();
                smearA.reset();
                smearB.reset();
                slew = 0.0f;
                sideNoise.reset();
                sideHpA.reset();
                sideHpB.reset();
                sideLp.reset();
            }
        };

        std::array<Channel, 2> channels;

        // -- shared ---------------------------------------------------------
        synth::NoiseExciter   coreNoise;
        OnePoleTPT            coreHpA, coreHpB, coreLp;
        Resonator             coreRing;
        Follower              duckEnv;      // fast: what the noise hides behind
        Follower              refEnv;       // slow: the erosion reference
        synth::DriftGenerator drift;

        Surface surface;                    // in use, morphing towards the target
        bool    surfacePrimed = false;
        int     noiseType = 1;
        float   lastDrift = 0.0f;           // drives this block's filter moves

        Smoothed toneAmt, noiseAmt, wearAmt, driftAmt, smearAmt, engage;

        void prepare (const EngineSpec& spec)
        {
            sampleRate = juce::jmax (8000.0, spec.sampleRate);

            const int maxDelay = (int) (sampleRate * 0.006) + 8;

            channels[0].prepare (sampleRate, 0x50A71A01u, maxDelay);
            channels[1].prepare (sampleRate, 0x50A71A02u, maxDelay);

            coreNoise.prepare (sampleRate, 0x50A71AC0u);
            coreHpA.setCutoff (600.0f, sampleRate);
            coreHpB.setCutoff (600.0f, sampleRate);
            coreLp .setCutoff (10000.0f, sampleRate);
            coreRing.set (4000.0f, 2.0f, sampleRate);

            duckEnv.prepare (0.005f, 0.250f, sampleRate);
            refEnv .prepare (0.030f, 0.600f, sampleRate);

            drift.prepare (sampleRate, 0x50A71AD1u);

            surfacePrimed = false;
            reset();
        }

        void reset() noexcept
        {
            for (auto& c : channels)
                c.reset();

            coreNoise.reset();
            coreHpA.reset();
            coreHpB.reset();
            coreLp.reset();
            coreRing.reset();
            duckEnv.reset();
            refEnv.reset();
            drift.reset();

            lastDrift = 0.0f;

            toneAmt.reset();
            noiseAmt.reset();
            wearAmt.reset();
            driftAmt.reset();
            smearAmt.reset();
            engage.reset();
        }

        /** One channel of the surface.  Everything that must stay identical
            between the two channels is computed by the caller and passed in. */
        forcedinline float processChannel (Channel& c, float x,
                                           float tone, float wear, float smear,
                                           float bluntStep, float erosionThr,
                                           float noiseCore, float noiseLevel,
                                           const Surface& s) noexcept
        {
            // 1. four complementary bands.  These sum back to x exactly.
            float low, mid, high;
            c.bands.split (x, low, mid, high);

            float lowMid, upperMid;
            c.lowMidSplit.split (mid, lowMid, upperMid);

            // 2. the static colour of the surface
            low      *= s.lowGain;
            lowMid   *= s.lowMidGain;
            upperMid *= s.upperMidGain;
            high     *= s.highGain;

            // 3. WEAR.  Erosion of detail, confined to the top of the spectrum.
            high = c.ceiling.lowpass (high);

            const float he = c.hfEnv.process (std::abs (high));

            // Downward expansion: 1 for loud detail, kErosionFloor for none.
            // The +kEps pair makes this exactly 1 when the threshold is 0, so
            // WEAR at zero is transparent rather than nearly so.
            const float erosion = (he + kErosionFloor * erosionThr + kEps)
                                / (he + erosionThr + kEps);
            high *= erosion;

            if (bluntStep > 0.0f)
            {
                const float delta = juce::jlimit (-bluntStep, bluntStep, upperMid - c.slew);
                c.slew = flush (c.slew + delta);
                upperMid = lerp (upperMid, c.slew, wear);
            }

            // 4. recombine.  The low band is never smeared: see the file header.
            float body = lowMid + upperMid + high;

            if (smear > 0.0005f)
            {
                const float diffused = c.smearB.process (c.smearA.process (body));
                body = lerp (body, diffused, smear);
            }

            float y = low + body;

            // 5. TONE
            y = c.tone.process (y, tone);

            // 6. NOISE.  The core is shared between the channels; only the side
            //    component differs, and it is high-passed twice at 900 Hz so
            //    that nothing decorrelated reaches the low end.
            float side = c.sideNoise.process (0);
            side = c.sideHpB.highpass (c.sideHpA.highpass (side));
            side = c.sideLp.lowpass (side);

            y += (noiseCore + side * s.noiseSpread) * noiseLevel;

            return y;
        }

        void process (juce::AudioBuffer<float>& buffer, const ParameterRegistry& p,
                      const MacroState& m) noexcept
        {
            const int n = juce::jmin (m.numSamples, buffer.getNumSamples());

            if (n <= 0 || buffer.getNumChannels() < 1)
                return;

            const bool on = p.flag (PID::patinaOn);

            // -- exact bypass -----------------------------------------------
            //  Off, and the fade has already run out: return without touching
            //  the buffer.  The output is the input, sample for sample.
            if (! on && engage.value() <= 1.0e-4f)
            {
                engage.hold (0.0f);
                return;
            }

            // -- read the registry once -------------------------------------
            const int   profile = juce::jlimit (0, kNumProfiles - 1, p.choice (PID::patinaProfile));
            const float tone    = juce::jlimit (-1.0f, 1.0f, p.raw (PID::patinaTone));
            const float noise   = juce::jlimit (0.0f, 1.0f, p.raw (PID::patinaNoise));
            const float wear    = juce::jlimit (0.0f, 1.0f, p.raw (PID::patinaWear));
            const float drft    = juce::jlimit (0.0f, 1.0f, p.raw (PID::patinaDrift));

            const float age      = juce::jlimit (0.0f, 1.0f, m.age);
            const float grit     = juce::jlimit (0.0f, 1.0f, m.grit);
            const float movement = juce::jlimit (0.0f, 1.0f, m.movement);
            const float alter    = juce::jlimit (0.0f, 1.0f, m.alterAmount);

            // -- which surface, and how far towards its alternate ------------
            const Surface& base = surfaceFor (profile);
            const Surface& alt  = surfaceFor (kAlternateProfile[profile]);
            const Surface target = blendSurface (base, alt, alter * 0.6f);

            const float morphCoef = blockCoefficient (0.040f, n, sampleRate);

            if (! surfacePrimed)
            {
                surface = target;
                surfacePrimed = true;
            }
            else
            {
                surface.approach (target, morphCoef);
            }

            noiseType = noiseTypeFor (profile);

            // -- macro response ---------------------------------------------
            const float wearTarget  = juce::jlimit (0.0f, 1.0f, wear  * surface.wearScale  + age      * 0.35f);
            const float noiseTarget = juce::jlimit (0.0f, 1.0f, noise * surface.noiseScale + grit     * 0.30f);
            const float driftTarget = juce::jlimit (0.0f, 1.0f, drft  * surface.driftScale + movement * 0.30f);
            const float toneTarget  = juce::jlimit (-1.0f, 1.0f, tone + surface.toneBias);

            const float ctrlCoef   = blockCoefficient (0.020f, n, sampleRate);
            const float engageCoef = blockCoefficient (0.008f, n, sampleRate);

            toneAmt .set (toneTarget,  n, ctrlCoef);
            noiseAmt.set (noiseTarget, n, ctrlCoef);
            wearAmt .set (wearTarget,  n, ctrlCoef);
            driftAmt.set (driftTarget, n, ctrlCoef);
            smearAmt.set (surface.smear * (0.35f + 0.65f * wearTarget), n, ctrlCoef);
            engage  .set (on ? 1.0f : 0.0f, n, engageCoef);

            // -- filters that move at block rate ----------------------------
            //  The drift is slow by construction (at most 0.6 Hz), so a block
            //  is at worst 0.03 octaves of movement even at 1024 samples and
            //  44.1 kHz.  Nothing here steps audibly, and no filter state is
            //  reset, so no coefficient change can click.
            const float driftNow = lastDrift;

            const float ceilingHz = juce::jlimit (800.0f, (float) (sampleRate * 0.45),
                                                  lerp (surface.ceilingHz, surface.ceilingFloorHz,
                                                        wearAmt.value())
                                                    * exp2Fast (driftNow * surface.driftToCeiling));

            const float noiseTopHz = juce::jlimit (400.0f, (float) (sampleRate * 0.45),
                                                   surface.noiseHighHz
                                                     * exp2Fast (driftNow * surface.noiseMove * 0.5f));

            for (auto& c : channels)
            {
                c.ceiling.setCutoff (ceilingHz, sampleRate);
                c.sideLp.setCutoff (noiseTopHz, sampleRate);
                c.sideHpA.setCutoff (juce::jmax (900.0f, surface.noiseLowHz), sampleRate);
                c.sideHpB.setCutoff (juce::jmax (900.0f, surface.noiseLowHz), sampleRate);

                const float apCoef = juce::jlimit (0.30f, 0.80f, 0.62f + driftNow * 0.08f);
                c.smearA.setCoefficient (apCoef);
                c.smearB.setCoefficient (apCoef * 0.92f);
            }

            coreHpA.setCutoff (surface.noiseLowHz, sampleRate);
            coreHpB.setCutoff (surface.noiseLowHz, sampleRate);
            coreLp .setCutoff (noiseTopHz, sampleRate);
            coreRing.set (surface.noiseRingHz, surface.noiseRingQ, sampleRate);

            const float ringNorm = 1.0f / juce::jlimit (0.5f, 20.0f, surface.noiseRingQ);

            // -- makeup for the profile's band gains ------------------------
            const float bandPower = kBandPower[0] * surface.lowGain      * surface.lowGain
                                  + kBandPower[1] * surface.lowMidGain   * surface.lowMidGain
                                  + kBandPower[2] * surface.upperMidGain * surface.upperMidGain
                                  + kBandPower[3] * surface.highGain     * surface.highGain;

            const float makeup = juce::jlimit (0.7f, 1.4f,
                                               1.0f / std::sqrt (juce::jmax (0.05f, bandPower)));

            // -- drift rate and how much Breath is mixed into it ------------
            const float driftRate   = juce::jlimit (0.01f, 0.60f,
                                                    surface.driftRateHz * (1.0f + movement * 0.8f));
            const float breathBlend = 0.35f * movement;

            const float bluntBase = surface.bluntRate / (float) sampleRate;

            // -- the block ---------------------------------------------------
            const bool  stereo = buffer.getNumChannels() > 1;
            float* const l = buffer.getWritePointer (0);
            float* const r = stereo ? buffer.getWritePointer (1) : nullptr;

            for (int i = 0; i < n; ++i)
            {
                const float dryL = l[i];
                const float dryR = stereo ? r[i] : dryL;

                // the surface's own slow instability
                const float wander = lerp (drift.process (driftRate), m.breathAt (i), breathBlend)
                                   * driftAmt.at (i);

                // what the noise has to live with
                const float mono = 0.5f * (dryL + dryR);
                const float rect = std::abs (mono);
                const float fast = duckEnv.process (rect);
                const float slow = refEnv.process (rect);

                const float presence = fast / (fast + kDuckKnee);      // 0 in gaps, 1 when loud
                const float ducked   = 1.0f - surface.noiseDuck * presence;
                const float excited  = presence * 1.8f;

                const float noiseLevel = juce::jmax (0.0f,
                        noiseAmt.at (i) * surface.noiseGain
                      * lerp (ducked, excited, surface.noiseExcite)
                      * (1.0f + surface.noiseMove * wander * 0.9f));

                // the correlated core, shared by both channels
                float core = coreNoise.process (noiseType);
                core = coreHpB.highpass (coreHpA.highpass (core));
                core = coreLp.lowpass (core);
                core = lerp (core, coreRing.process (core) * ringNorm, surface.noiseRing);

                const float w = wearAmt.at (i);
                const float toneNow = juce::jlimit (-1.0f, 1.0f,
                                                    toneAmt.at (i) + wander * surface.driftToTone);
                const float smearNow = smearAmt.at (i);
                const float erosionThr = kErosionRefScale * slow * surface.erosionDepth * w;
                const float bluntStep = bluntBase * juce::jmax (0.02f, slow);

                const float wetL = processChannel (channels[0], dryL, toneNow, w, smearNow,
                                                   bluntStep, erosionThr, core, noiseLevel, surface)
                                 * makeup;

                const float e = juce::jlimit (0.0f, 1.0f, engage.at (i));

                l[i] = guard (lerp (dryL, wetL, e));

                if (stereo)
                {
                    const float wetR = processChannel (channels[1], dryR, toneNow, w, smearNow,
                                                       bluntStep, erosionThr, core, noiseLevel, surface)
                                     * makeup;

                    r[i] = guard (lerp (dryR, wetR, e));
                }

                lastDrift = wander;
            }
        }
    };

    // =======================================================================
    PatinaEngine::PatinaEngine() : impl (std::make_unique<Impl>()) {}
    PatinaEngine::~PatinaEngine() = default;

    void PatinaEngine::prepare (const EngineSpec& spec) { impl->prepare (spec); }
    void PatinaEngine::reset()                          { impl->reset(); }

    void PatinaEngine::process (juce::AudioBuffer<float>& buffer, const ParameterRegistry& p,
                                const MacroState& m)
    {
        impl->process (buffer, p, m);
    }
}
