#include "MacroPanel.h"

namespace nacar::ui
{
    using namespace layout;

    // =======================================================================
    //  Numbers the reference implies but Layout.h does not carry.
    //
    //  Layout.h fixes every knob centre, radius and baseline in the panel.
    //  What is missing is the padding a knob *component* needs around its body
    //  and the two spans below, so they live here with their reason, per the
    //  contract.
    // =======================================================================
    namespace
    {
        /// A knob draws its seat groove and its violet value arc *outside* the
        /// body radius in macro::KnobSpec, so every knob component is its
        /// layout::knobBounds() grown by this much.  Growing uniformly keeps
        /// the cap centred on the spec centre.
        constexpr float knobPadding = 10.0f;

        /// Half the generation-selector row pitch: macro::genY steps by 34.
        constexpr float genRowHalf = 17.0f;

        /// Right edge of the generation column, panel-local.  Layout.h fixes
        /// macro::genDotX and macro::genLabelX but not where the column stops;
        /// 300 leaves the panel's own 17 px right margin intact.
        constexpr float genRight = 300.0f;

        /// Height of the row a scale legend occupies.  macro::legendSize is the
        /// type size; the row has to be tall enough for ui::scaleLegend to set
        /// the two ends and the hairline between them.
        constexpr float legendRowHeight = 12.0f;

        /// Distance from the legend row's vertical centre down to the baseline
        /// quoted in macro::legend*Base.  This assumes ui::scaleLegend sets its
        /// two ends centred in the area it is handed, which is the only
        /// placement its signature allows.
        constexpr float legendBaselineDrop = 3.0f;

        /// WORLD's INTIMATE - EXPANSIVE will not fit the 96 px horizontal extent
        /// of a 48 px knob, so its legend is allowed to run 20 px past the body
        /// on each side.  It still clears WEIGHT's column, which starts at 180.
        constexpr float worldLegendWiden = 20.0f;

        juce::Rectangle<int> knobArea (const macro::KnobSpec& k)
        {
            return knobBounds (k).expanded (knobPadding).toNearestInt();
        }

        RectF legendArea (const macro::KnobSpec& k, float baseline, float widen = 0.0f)
        {
            const float halfWidth = k.radius + widen;

            return { k.centre.x - halfWidth,
                     baseline - legendBaselineDrop - legendRowHeight * 0.5f,
                     halfWidth * 2.0f,
                     legendRowHeight };
        }

        juce::String paramTip (PID p)
        {
            const auto& d = ParameterRegistry::definition (p);
            return juce::String (d.name) + "\n" + juce::String (d.tooltip);
        }
    }

    // =======================================================================
    //  MacroPanel
    // =======================================================================
    MacroPanel::MacroPanel (NacarProcessor& p, EditorHost& h)
        : processor (p), host (h),
          memoryKnob    (p.getParameters(), PID::macroMemory),
          characterKnob (p.getParameters(), PID::macroCharacter),
          motionKnob    (p.getParameters(), PID::macroMotion),
          worldKnob     (p.getParameters(), PID::macroWorld),
          weightKnob    (p.getParameters(), PID::macroWeight),
          alterKnob     (p.getParameters(), PID::macroAlter),

          // RANDOM is the only dark cap in the panel.  It is visually
          // subordinate on purpose: it is a depth control, not MUTATE.
          randomKnob    (p.getParameters(), PID::randomAmount, NacarKnob::Style::dark),

          generation         (p.getParameters(), PID::memoryGen),
          weightModeSelector ({ "SUB", "BODY", "AIR" }, SegmentedControl::Style::recessed)
    {
        configureKnob (memoryKnob,    "MEMORY");
        configureKnob (characterKnob, "CHARACTER");
        configureKnob (motionKnob,    "MOTION");
        configureKnob (worldKnob,     "WORLD");
        configureKnob (weightKnob,    "WEIGHT");
        configureKnob (alterKnob,     "ALTER");
        configureKnob (randomKnob,    "RANDOM");

        // --------------------------------------------------------------------
        //  RANDOM, explicitly.
        //
        //  This knob sets *how far* a bounded variation is allowed to wander.
        //  A click on it does nothing but grab the value - there is no trigger
        //  here, and there is not meant to be one: it is not MUTATE and it is
        //  not a dice button.
        //
        //  NOT WIRED IN V1 PHASE 2: nothing yet consumes PID::randomAmount,
        //  because the thing that applies the variation does not exist.  The
        //  trigger and the variation engine belong in Source/Mutation/ (the
        //  bounded-variation pass that reads randomAmount as its depth); until
        //  that lands the parameter stores and automates correctly but has no
        //  audible effect.
        // --------------------------------------------------------------------
        randomKnob.setTooltip (paramTip (PID::randomAmount));

        generation.setTooltip (paramTip (PID::memoryGen));
        addAndMakeVisible (generation);

        // 7.5 px / 0.12 em - the same quiet setting as the scale legends, which
        // is what macro::legendSize and macro::legendTrack already carry.
        weightModeSelector.setTextSize (macro::legendSize, macro::legendTrack);
        weightModeSelector.bindTo (processor.getParameters(), PID::weightMode);
        weightModeSelector.setTooltip (paramTip (PID::weightMode));
        addAndMakeVisible (weightModeSelector);

        // The macro panel asks nothing of the editor in V1; the reference is
        // part of the region-component contract.
        juce::ignoreUnused (host);
    }

    MacroPanel::~MacroPanel() = default;

    void MacroPanel::configureKnob (NacarKnob& knob, const juce::String& name)
    {
        // The knob owns its own name: it paints it beneath the cap, so the
        // panel never draws a knob label itself.
        knob.setLabel (name, macro::knobLabelSize, macro::knobLabelTrack);
        addAndMakeVisible (knob);
    }

    void MacroPanel::paint (juce::Graphics& g)
    {
        const auto b = getLocalBounds().toFloat();

        theme::contactShadow (g, b, radiusPanel);
        theme::ceramicSurface (g, b, radiusPanel);

        // --------------------------------------------------------------------
        //  Scale legends.
        //
        //  Drawn here rather than handed to NacarKnob::setScaleLegend.  Every
        //  macro::legend*Base sits below the knob component that would have to
        //  draw it - CHARACTER's component ends at y 371 (centre 313, radius 48,
        //  grown by knobPadding) and its legend baseline is 398 - and WORLD's
        //  INTIMATE - EXPANSIVE is wider than its 96 px body besides.  Drawing
        //  in the panel is the only way to land on the transcribed baselines.
        // --------------------------------------------------------------------
        scaleLegend (g, "CLEAN", "WORN",
                     legendArea (macro::character, macro::legendCharacterBase));

        scaleLegend (g, "STILL", "ALIVE",
                     legendArea (macro::motion, macro::legendMotionBase));

        scaleLegend (g, "INTIMATE", "EXPANSIVE",
                     legendArea (macro::world, macro::legendWorldBase, worldLegendWiden));
    }

    void MacroPanel::resized()
    {
        memoryKnob   .setBounds (knobArea (macro::memory));
        characterKnob.setBounds (knobArea (macro::character));
        motionKnob   .setBounds (knobArea (macro::motion));
        worldKnob    .setBounds (knobArea (macro::world));
        weightKnob   .setBounds (knobArea (macro::weight));
        alterKnob    .setBounds (knobArea (macro::alter));
        randomKnob   .setBounds (knobArea (macro::random));

        // The I / II / III / IV column beside MEMORY: the four dot rows, from
        // the dots out to the right edge of the column.
        generation.setBounds (RectF (macro::genDotX,
                                     macro::genY[0] - genRowHalf,
                                     genRight - macro::genDotX,
                                     (macro::genY[3] - macro::genY[0]) + genRowHalf * 2.0f)
                                  .toNearestInt());

        weightModeSelector.setBounds (macro::weightModes.toNearestInt());
    }
}
