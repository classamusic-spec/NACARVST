#include "FXPage.h"

namespace nacar::ui
{
    // =======================================================================
    //  Page geometry.  Layout.h is frozen and does not describe the deep-edit
    //  pages, so the numbers for this one are named here.  The page is handed
    //  875 x 818, leaving a content area of 839 x 724 below the title block:
    //  three cards of 273 across, two of 357 down, with 10 between.
    // =======================================================================
    static constexpr int cardColumns = 3;
    static constexpr int cardRows    = 2;
    static constexpr int cardGap     = 10;

    static constexpr int selectorH   = 22;   // a segmented control or pill row
    static constexpr int toggleW     = 26;
    static constexpr int toggleH     = 14;
    static constexpr int footnoteH   = 16;

    // Six cards, forty-five controls.  Every card keeps the same 24 px cap
    // except GRAIN, which carries thirteen controls and drops to 20 so its
    // four-column grid still breathes.
    static constexpr float fxKnobR    = 24.0f;
    static constexpr float grainKnobR = 20.0f;

    // -----------------------------------------------------------------------
    /** The module's one-line description, taken from its own enable parameter
        so the card can never describe something the parameter list does not. */
    static juce::String moduleNote (PID enablePid)
    {
        return juce::String (ParameterRegistry::definition (enablePid).tooltip)
                   .upToLastOccurrenceOf (".", false, false)
                   .toUpperCase();
    }

    // =======================================================================
    //  FXPage
    // =======================================================================
    FXPage::FXPage (NacarProcessor& p, EditorHost& h)
        : PageSurface (p, h, "FX CHAIN",
                       juce::String::fromUTF8 ("DISPLAY ORDER IS DSP ORDER \xc2\xb7 SIX MODULES"),
                       Material::glass)
    {
        modules[0] = { "RETRO",  icons::Icon::cassette,   PID::retroOn };
        modules[1] = { "CRUSH",  icons::Icon::dotMatrix,  PID::crushOn };
        modules[2] = { "FILTER", icons::Icon::filterCurve, PID::fxFilterOn };
        modules[3] = { "REWIND", icons::Icon::rewind,     PID::rewindOn };
        modules[4] = { "GRAIN",  icons::Icon::concentric, PID::grainFxOn };
        modules[5] = { "SPACE",  icons::Icon::planet,     PID::spaceOn };

        for (auto& m : modules)
            m.power = &addPower (m.enable);

        // -- RETRO ----------------------------------------------------------
        addKnob (PID::retroEra,   "ERA");
        addKnob (PID::retroAge,   "AGE");
        addKnob (PID::retroDrift, "DRIFT");
        addKnob (PID::retroWear,  "WEAR");
        addKnob (PID::retroTone,  "TONE", true);      // bipolar tilt
        addKnob (PID::retroNoise, "NOISE");
        addKnob (PID::retroMix,   "MIX");

        // -- CRUSH ----------------------------------------------------------
        addKnob (PID::crushAmount, "AMOUNT");
        addKnob (PID::crushBits,   "BITS");
        addKnob (PID::crushRate,   "RATE");
        addKnob (PID::crushJitter, "JITTER");
        addKnob (PID::crushDrive,  "DRIVE");
        addKnob (PID::crushTone,   "TONE", true);     // bipolar tilt
        addKnob (PID::crushMix,    "MIX");

        // -- FILTER ---------------------------------------------------------
        filterMode = &addSegmented (PID::fxFilterMode,
                                    { "LP", "HP", "BP", "NTCH", "COMB", "FORM" });
        addKnob (PID::fxFilterCutoff, "CUTOFF");
        addKnob (PID::fxFilterRes,    "RES");
        addKnob (PID::fxFilterDrive,  "DRIVE");
        addKnob (PID::fxFilterMorph,  "MORPH");
        addKnob (PID::fxFilterMotion, "MOTION");
        addKnob (PID::fxFilterMix,    "MIX");

        // -- REWIND ---------------------------------------------------------
        rewindMode     = &addSegmented (PID::rewindMode);
        rewindDivision = &addSegmented (PID::rewindDivision);
        rewindSync     = &addToggle (PID::rewindSync);
        addKnob (PID::rewindLength, "FREE");
        addKnob (PID::rewindCurve,  "CURVE");
        addKnob (PID::rewindSpeed,  "SPEED");
        addKnob (PID::rewindTail,   "TAIL");
        addKnob (PID::rewindMix,    "MIX");

        // -- GRAIN ----------------------------------------------------------
        // Seven pitch modes is one more than a six-segment control reads
        // cleanly at this width, so the pitch mode is a pill with a popup.
        grainPitchMode = &addChoicePill (PID::grainPitchMode);
        grainWindow    = &addSegmented (PID::grainWindow,
                                        { "HANN", "TUKEY", "GAUSS", "EXPO", "PERC" });
        grainFreeze    = &addToggle (PID::grainFreeze);

        addKnob (PID::grainScatter,   "SCATTER");
        addKnob (PID::grainSize,      "SIZE");
        addKnob (PID::grainDensity,   "DENSITY");
        addKnob (PID::grainPosition,  "POSITION");
        addKnob (PID::grainPitch,     "PITCH");
        addKnob (PID::grainSpread,    "SPREAD");
        addKnob (PID::grainJitter,    "JITTER");
        addKnob (PID::grainDirection, "REVERSE");
        addKnob (PID::grainFeedback,  "FEEDBACK");
        addKnob (PID::grainMix,       "MIX");

        // -- SPACE ----------------------------------------------------------
        spaceCharacter = &addSegmented (PID::spaceCharacter,
                                        { "ROOM", "CHAMB", "DARK", "DIST", "INF" });
        addKnob (PID::spaceDistance, "DISTANCE");
        addKnob (PID::spaceSize,     "SIZE");
        addKnob (PID::spaceDecay,    "DECAY");
        addKnob (PID::spaceFog,      "FOG");
        addKnob (PID::spaceLight,    "LIGHT", true);   // bipolar brightness
        addKnob (PID::spacePreDelay, "PRE-DELAY");
        addKnob (PID::spaceMix,      "MIX");
    }

    FXPage::~FXPage() = default;

    // -----------------------------------------------------------------------
    void FXPage::resized()
    {
        beginLayout();

        auto content = contentArea();

        const int cardW = (content.getWidth() - (cardColumns - 1) * cardGap) / cardColumns;
        const int cardH = (content.getHeight() - (cardRows - 1) * cardGap) / cardRows;

        for (int i = 0; i < numModules; ++i)
        {
            const int row = i / cardColumns;
            const int col = i % cardColumns;

            modules[i].bounds = { content.getX() + col * (cardW + cardGap),
                                  content.getY() + row * (cardH + cardGap),
                                  cardW, cardH };

            auto& m = modules[i];
            m.power->setBounds (m.bounds.getRight() - 34, m.bounds.getY() + 4, 22, 22);

            auto in = inside (m.bounds);
            m.footnote = in.removeFromBottom (footnoteH);

            switch (i)
            {
                case 0:     // RETRO
                case 1:     // CRUSH
                    knobGrid (in, 3, fxKnobR, 7);
                    break;

                case 2:     // FILTER
                    filterMode->setBounds (in.removeFromTop (selectorH));
                    in.removeFromTop (6);
                    knobGrid (in, 3, fxKnobR, 6);
                    break;

                case 3:     // REWIND
                {
                    rewindMode->setBounds (in.removeFromTop (selectorH));
                    in.removeFromTop (6);

                    auto syncRow = in.removeFromTop (selectorH);
                    rewindSync->setBounds (syncRow.getRight() - toggleW,
                                           syncRow.getCentreY() - toggleH / 2, toggleW, toggleH);
                    rewindSyncCaption = syncRow.removeFromRight (toggleW + 40);
                    rewindDivision->setBounds (syncRow.withTrimmedRight (8));

                    in.removeFromTop (6);
                    knobGrid (in, 3, fxKnobR, 5);
                    break;
                }

                case 4:     // GRAIN
                {
                    auto pitchRow = in.removeFromTop (selectorH);
                    grainPitchCaption = pitchRow.removeFromLeft (44);
                    grainPitchMode->setBounds (pitchRow);
                    in.removeFromTop (6);

                    auto windowRow = in.removeFromTop (selectorH);
                    grainFreeze->setBounds (windowRow.getRight() - toggleW,
                                            windowRow.getCentreY() - toggleH / 2, toggleW, toggleH);
                    grainFreezeCaption = windowRow.removeFromRight (toggleW + 46);
                    grainWindow->setBounds (windowRow.withTrimmedRight (8));

                    in.removeFromTop (6);
                    knobGrid (in, 4, grainKnobR, 10);
                    break;
                }

                case 5:     // SPACE
                    spaceCharacter->setBounds (in.removeFromTop (selectorH));
                    in.removeFromTop (6);
                    knobGrid (in, 3, fxKnobR, 7);
                    break;

                default:
                    break;
            }
        }
    }

    // -----------------------------------------------------------------------
    void FXPage::paint (juce::Graphics& g)
    {
        paintChrome (g);

        for (const auto& m : modules)
        {
            section (g, m.bounds.toFloat(), m.name, m.icon);

            // A powered module carries the mint rule the FX cards use on the
            // MAIN page; a bypassed one keeps the glass hairline.
            const bool powered = params.flag (m.enable);

            g.setColour (powered ? theme::mint.withAlpha (0.45f) : theme::glassEdge);
            g.drawLine ((float) m.bounds.getX() + page::sectionPad,
                        (float) m.bounds.getY() + page::captionH,
                        (float) m.bounds.getRight() - page::sectionPad,
                        (float) m.bounds.getY() + page::captionH, 1.0f);

            text (g, moduleNote (m.enable),
                  { (float) m.footnote.getX(), (float) m.footnote.getBottom() - 2.0f },
                  page::noteSize, page::noteTrack, inkFaint());
        }

        text (g, "SYNC", { (float) rewindSyncCaption.getX(),
                           (float) rewindSyncCaption.getCentreY() + 3.0f },
              page::captionSize, 0.14f, inkSoft());

        text (g, "FREEZE", { (float) grainFreezeCaption.getX(),
                             (float) grainFreezeCaption.getCentreY() + 3.0f },
              page::captionSize, 0.14f, inkSoft());

        text (g, "PITCH", { (float) grainPitchCaption.getX(),
                            (float) grainPitchCaption.getCentreY() + 3.0f },
              page::captionSize, 0.14f, inkSoft());
    }
}
