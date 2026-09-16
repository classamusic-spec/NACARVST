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
        /// ui::NacarKnob lays its seat groove and value arc outside the cap, and
        /// derives capRadius() from half its component's shorter side minus that
        /// allowance - 1.5 clearance + 5 groove + 0.5 lip.  Growing
        /// layout::knobBounds() by exactly the allowance therefore puts the cap
        /// on the spec centre at the spec radius, with the groove in the margin.
        constexpr float knobSeatAllowance = 7.0f;

        /// Extra height under every knob.  NacarKnob hangs its cap from the top
        /// of the component and drops the name below it, so the parent has to
        /// size the component tall enough to contain the name and its
        /// descenders - see the note in ui::NacarKnob::paint.
        constexpr float knobLabelRoom = 20.0f;

        /// Half the generation-selector row pitch: macro::genY steps by 34.
        constexpr float genRowHalf = 17.0f;

        /// ui::GenerationSelector insets its dot from its own left edge so the
        /// active dot's glow is not clipped, and lays the numerals out relative
        /// to that.  Pulling the component left by the same amount lands the dot
        /// on macro::genDotX and the numerals on macro::genLabelX.
        constexpr float genDotInset = macro::genDotRActive + 1.0f;

        /// Right edge of the generation column, panel-local.  Layout.h fixes
        /// macro::genDotX and macro::genLabelX but not where the column stops;
        /// 300 leaves the panel's own 17 px right margin intact.
        constexpr float genRight = 300.0f;

        /// Height of the row a scale legend occupies; ui::scaleLegend hangs its
        /// hairline on the row's vertical centre.
        constexpr float legendRowHeight = 12.0f;

        /// WORLD's INTIMATE - EXPANSIVE does not fit the 96 px horizontal extent
        /// of a 48 px knob, so its legend runs 20 px past the body on each side.
        /// It still clears WEIGHT's column, which starts at x 173.
        constexpr float worldLegendWiden = 20.0f;

        juce::Rectangle<int> knobArea (const macro::KnobSpec& k)
        {
            const auto seat = knobBounds (k).expanded (knobSeatAllowance);

            // Grown downwards only: the cap hangs from the top, so the extra
            // height becomes label room instead of moving the cap.
            return seat.withHeight (seat.getHeight() + knobLabelRoom).toNearestInt();
        }

        RectF legendArea (const macro::KnobSpec& k, float baseline, float widen = 0.0f)
        {
            // ui::scaleLegend derives its baseline from the vertical centre of
            // the area it is handed, so the area is centred on whatever puts the
            // type on the baseline the reference transcribes.
            const auto  font    = theme::label (macro::legendSize);
            const float centreY = baseline - (font.getAscent() - font.getHeight() * 0.5f);

            return RectF ((k.radius + widen) * 2.0f, legendRowHeight)
                       .withCentre ({ k.centre.x, centreY });
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
        //  Drawn here rather than handed to NacarKnob::setScaleLegend.  The knob
        //  places a legend a fixed gap below the name it paints, which lands it
        //  4 px above macro::legendCharacterBase and 14 px above
        //  macro::legendWorldBase; and WORLD's INTIMATE - EXPANSIVE is wider
        //  than its 96 px body besides.  Drawing all three in the panel is the
        //  only way to land every one on its transcribed baseline.
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
        generation.setBounds (RectF (macro::genDotX - genDotInset,
                                     macro::genY[0] - genRowHalf,
                                     genRight - (macro::genDotX - genDotInset),
                                     (macro::genY[3] - macro::genY[0]) + genRowHalf * 2.0f)
                                  .toNearestInt());

        weightModeSelector.setBounds (macro::weightModes.toNearestInt());
    }
}
