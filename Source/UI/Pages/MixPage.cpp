#include "MixPage.h"

namespace nacar::ui
{
    namespace
    {
        // Page-local metrics: these describe the inside of a mix strip, a
        // surface Layout.h knows nothing about.
        constexpr float stripGap    = 10.0f;
        constexpr float knobRadius  = 21.0f;
        constexpr float meterH      = 46.0f;
        constexpr int   meterSegments = 40;
        constexpr float meterFloorDb  = -48.0f;
        constexpr float meterWarnDb   = -5.0f;
    }

    MixPage::MixPage (NacarProcessor& p, EditorHost& h)
        : PageSurface (p, h, "MIX", "BALANCE & GAIN STAGING", Material::glass)
    {
        // --------------------------------------------------------------
        //  SYNTH - the voice mixer and the stereo architecture
        // --------------------------------------------------------------
        addKnob (PID::oscALevel,  "OSC A");
        addKnob (PID::oscBLevel,  "OSC B");
        addKnob (PID::oscCLevel,  "OSC C");
        addKnob (PID::subLevel,   "SUB");
        addKnob (PID::noiseLevel, "NOISE");
        addKnob (PID::synthWidth, "WIDTH");
        addKnob (PID::lowMonoFreq, "LOW MONO");
        addKnob (PID::highWidth,  "HIGH WIDTH");
        strips[0] = { "SYNTH", "VOICE MIXER  \xc2\xb7  STEREO ARCHITECTURE", 8, 4, {} };

        // --------------------------------------------------------------
        //  SAMPLE
        // --------------------------------------------------------------
        addKnob (PID::sampleGain,     "GAIN");
        addKnob (PID::sampleTune,     "TUNE", true);
        addKnob (PID::sampleKeyTrack, "KEY TRK");
        strips[1] = { "SAMPLE", "", 3, 3, {} };

        // --------------------------------------------------------------
        //  RESONATOR / SPECTRAL - one control each until their engines grow
        // --------------------------------------------------------------
        addKnob (PID::resMix,  "RESONATOR");
        addKnob (PID::specMix, "SPECTRAL");
        strips[2] = { "OTHER SOURCES", "", 2, 2, {} };

        // --------------------------------------------------------------
        //  FX balance - the six chain mixes, in DSP order
        // --------------------------------------------------------------
        addKnob (PID::retroMix,    "RETRO");
        addKnob (PID::crushMix,    "CRUSH");
        addKnob (PID::fxFilterMix, "FILTER");
        addKnob (PID::rewindMix,   "REWIND");
        addKnob (PID::grainMix,    "GRAIN");
        addKnob (PID::spaceMix,    "SPACE");
        strips[3] = { "FX BALANCE", "DISPLAY ORDER IS DSP ORDER", 6, 6, {} };

        // --------------------------------------------------------------
        //  Atmosphere sends
        // --------------------------------------------------------------
        addKnob (PID::auraDistance, "AURA");
        addKnob (PID::shadowLevel,  "SHADOW");
        addKnob (PID::patinaNoise,  "PATINA");
        strips[4] = { "ATMOSPHERE", "", 3, 3, {} };

        // --------------------------------------------------------------
        //  Weight
        // --------------------------------------------------------------
        addKnob (PID::macroWeight,      "WEIGHT");
        addKnob (PID::weightHarmonics,  "HARMONICS");
        addKnob (PID::weightCompress,   "COMPRESS");
        strips[5] = { "WEIGHT", "SO IT SURVIVES A SMALL SPEAKER", 3, 3, {} };

        // --------------------------------------------------------------
        //  Output
        // --------------------------------------------------------------
        addKnob (PID::masterGain, "OUTPUT");
        strips[6] = { "OUTPUT", "", 1, 1, {} };

        setTickHz (24);   // the meter is the fastest thing on this page
    }

    MixPage::~MixPage() = default;

    void MixPage::tick()
    {
        const float l = processor.getMeterLevel (0);
        const float r = processor.getMeterLevel (1);

        if (std::abs (l - meterLevel[0]) > 0.002f || std::abs (r - meterLevel[1]) > 0.002f)
        {
            meterLevel[0] = l;
            meterLevel[1] = r;
            repaint (meterBounds);
        }
    }

    void MixPage::paintMeter (juce::Graphics& g, juce::Rectangle<float> area) const
    {
        previewWell (g, area);

        const auto inner = area.reduced (6.0f, 5.0f);
        const float rowH = (inner.getHeight() - 2.0f) * 0.5f;
        const float segW = (inner.getWidth() - (float) (meterSegments - 1)) / (float) meterSegments;

        for (int ch = 0; ch < 2; ++ch)
        {
            const float db = juce::Decibels::gainToDecibels (meterLevel[ch], meterFloorDb);
            const float lit = juce::jlimit (0.0f, 1.0f,
                                            (db - meterFloorDb) / (0.0f - meterFloorDb));

            const int litCount = (int) std::round (lit * (float) meterSegments);
            const float y = inner.getY() + (float) ch * (rowH + 2.0f);

            for (int s = 0; s < meterSegments; ++s)
            {
                const float t = (float) s / (float) (meterSegments - 1);
                const float segDb = meterFloorDb + t * (0.0f - meterFloorDb);

                juce::Colour c;

                if (s < litCount)
                {
                    // Mint through the working range, shading towards the
                    // violet accent at the top rather than introducing a third
                    // hue - the palette is locked to two.
                    c = theme::mintDeep.interpolatedWith (theme::mint, t);

                    if (segDb > meterWarnDb)
                        c = c.interpolatedWith (theme::violetDeep,
                                                (segDb - meterWarnDb) / (0.0f - meterWarnDb));
                }
                else
                {
                    c = theme::glassEdge.withAlpha (0.55f);
                }

                g.setColour (c);
                g.fillRect (inner.getX() + (float) s * (segW + 1.0f), y, segW, rowH);
            }
        }
    }

    void MixPage::paint (juce::Graphics& g)
    {
        paintChrome (g);

        for (const auto& s : strips)
        {
            if (s.bounds.isEmpty())
                continue;

            section (g, s.bounds.toFloat(), s.caption);

            if (s.note.isNotEmpty())
                text (g, s.note,
                      { (float) s.bounds.getRight() - page::sectionPad,
                        (float) s.bounds.getY() + 18.0f },
                      page::noteSize, page::noteTrack, inkFaint(),
                      juce::Justification::right,
                      (float) s.bounds.getWidth() - page::sectionPad * 2.0f);
        }

        if (! meterBounds.isEmpty())
            paintMeter (g, meterBounds.toFloat());

        // What this page cannot show, and why.
        const auto area = contentArea().toFloat();
        text (g, "NO PER-SOURCE GAIN OR PAN PARAMETERS EXIST YET  \xc2\xb7  "
                 "THESE STRIPS SHOW WHAT IS REAL",
              { area.getX(), area.getBottom() - 2.0f },
              page::noteSize, 0.14f, inkFaint());
    }

    void MixPage::resized()
    {
        beginLayout();

        auto area = contentArea();
        area.removeFromBottom (14);          // room for the footnote

        // Two columns.  The synth strip is the tallest because it is the only
        // one that is genuinely a mixer; everything else is a balance control.
        auto left  = area.removeFromLeft (area.getWidth() * 5 / 9);
        area.removeFromLeft ((int) stripGap);
        auto right = area;

        const int synthH = juce::jmax (120, left.getHeight() * 44 / 100);

        strips[0].bounds = left.removeFromTop (synthH);
        left.removeFromTop ((int) stripGap);

        const int remaining = (left.getHeight() - (int) stripGap * 2) / 3;
        strips[1].bounds = left.removeFromTop (remaining);
        left.removeFromTop ((int) stripGap);
        strips[2].bounds = left.removeFromTop (remaining);
        left.removeFromTop ((int) stripGap);
        strips[5].bounds = left;

        const int fxH = juce::jmax (110, right.getHeight() * 34 / 100);
        strips[3].bounds = right.removeFromTop (fxH);
        right.removeFromTop ((int) stripGap);

        const int atmosH = juce::jmax (100, right.getHeight() * 45 / 100);
        strips[4].bounds = right.removeFromTop (atmosH);
        right.removeFromTop ((int) stripGap);
        strips[6].bounds = right;

        // The knobs were declared in strip order, so one pass places them all.
        for (const auto& s : strips)
        {
            if (s.bounds.isEmpty() || s.numKnobs == 0)
                continue;

            auto inner = inside (s.bounds);

            // The output strip shares its interior with the meter.
            if (&s == &strips[6])
            {
                meterBounds = inner.removeFromBottom ((int) meterH);
                inner.removeFromBottom (6);
            }

            knobGrid (inner, s.columns, knobRadius, s.numKnobs);
        }
    }
}
