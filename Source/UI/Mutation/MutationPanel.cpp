#include "MutationPanel.h"

#include "../../Mutation/MutationRecipe.h"
#include "../../Harmony/Harmony.h"

// ===========================================================================
//  WHAT IS WIRED IN THIS FILE, AND WHAT IS NOT
//
//  The mutation engine does not exist.  It is Phase 21 and it will live in
//  Source/Mutation/.  Nothing in this file transforms audio, and nothing in
//  this file pretends to.  Read this block before reviewing anything below it.
//
//  LIVE - real, persisted state that the Phase 21 engine will read back:
//
//    * HARMONY   -> PID::harmonyMode      (SegmentedControl::bindTo)
//    * DISTANCE  -> PID::distanceMode     (SegmentedControl::bindTo)
//    * INTENT    -> PID::mutationIntent   (IntentSelector -> setFromUI)
//    * PRESERVE  -> PID::preservePitch .. PID::preserveAll
//                                         (PreserveLock -> ToggleSwitch::bindTo)
//    * MUTATE and AGAIN roll a seed with juce::Random, honour
//      SESSION/MUTATION/seedLocked, store it in SESSION/MUTATION/currentSeed,
//      append a RECIPE child under SESSION/MUTATION/HISTORY carrying
//      recipeSeed / recipeIntent / recipeHarmony / recipeDistance, and move
//      SESSION/MUTATION/historyIndex onto it.  That tree is serialised with
//      the session, so it survives a save and reload.
//
//      AGAIN differs from MUTATE in exactly one way, and it is a real
//      difference: it re-rolls the seed only, and takes intent, harmony and
//      distance from the most recent recipe instead of from the live
//      controls, so "again" means the same request with different dice.
//
//  STUBBED - the control responds, and says out loud what it did not do:
//
//    * MUTATE / AGAIN do not synthesise, resample or re-voice anything.
//      ids::recipePayload is written empty and ids::recipeScore is written
//      zero; both are the engine's to fill.  The status line says
//      "ENGINE PENDING (PHASE 21)" after every roll so the recorded recipe is
//      never mistaken for a rendered result.
//    * PRINT does not render.  There is no offline render path in the
//      processor yet, so it touches no audio and no file, and sets a status
//      line naming phase 22.
//    * MAKE INSTRUMENT does not build a multisampled instrument.  Same
//      treatment: no audio, no file, a status line naming phase 21.
//
//  WHERE THE REAL ENGINE HOOKS IN:
//
//    runMutation() is the single entry point, and it is the only place any of
//    this needs to change.  When Source/Mutation lands, every ValueTree write
//    below stays exactly as it is; the engine is invoked immediately after the
//    recipe has been appended, is handed that recipe, and writes its result
//    into ids::recipePayload and its rating into ids::recipeScore.  PRINT and
//    MAKE INSTRUMENT then replace their setStatus() calls with calls into the
//    render path.  Nothing else in this file is engine-shaped.
// ===========================================================================

namespace nacar::ui
{
    namespace mut = layout::mut;

    // =======================================================================
    //  Numbers the reference implies but the frozen Layout.h does not carry.
    //
    //  Per the contract, every one of these is a size, a span or a weight for
    //  an element whose position is already fixed by layout::mut.
    // =======================================================================
    namespace
    {
        /// JUCE clips a component's painting to its own bounds, and the 8 px
        /// chassis gap below the mutate panel belongs to no region, so the
        /// panel's contact shadow has to be drawn inside its own rectangle.
        /// One pixel is the whole budget, and it is enough to read as seated.
        constexpr float shadowInset = 1.0f;

        /// UI spec 6 gives the sparkle mark a centre but no size.  26 px puts
        /// it on the same optical weight as the 25 pt MUTATE beside it.
        constexpr float sparkleSize = 26.0f;

        /// Stroke weight for panel glyphs, expressed for a 24 px icon.
        constexpr float glyphStroke = 1.5f;

        /// Left-justified tracked runs only need the left edge of their box;
        /// this is simply a bound large enough never to clip the heading.
        constexpr float titleRunWidth = 420.0f;

        /// The four action buttons are 57 px tall, far larger than a standard
        /// pill, so layout::radiusPill (8) reads as a sharp corner on them.
        /// 12 keeps the same corner-to-height ratio the pills have.
        constexpr float actionCorner = 12.0f;

        /// MUTATE is the primary action in the instrument and is set larger
        /// than the three buttons beside it.  UI spec 6 fixes the rects but
        /// not the type; 11 / 0.16 em for MUTATE, 9.5 / 0.16 em for the rest.
        constexpr float mutateTextSize   = 11.0f;
        constexpr float mutateTextTrack  = 0.16f;
        constexpr float actionTextSize   = 9.5f;
        constexpr float actionTextTrack  = 0.16f;

        /// Icon height as a fraction of the button height (Widgets.h defaults
        /// to 0.5, which on a 57 px button would give a 28 px glyph - far too
        /// loud next to 11 pt type).  0.30 lands the glyph at about 17 px.
        constexpr float actionIconRatio = 0.30f;

        /// Gap between a pill's glyph and its label.  Widgets.cpp uses 8 for
        /// every pill in the instrument; MUTATE draws its own face and so has
        /// to state the same number rather than inherit it, or it would be the
        /// one button in the row whose glyph sits at a different distance.
        constexpr float actionIconGap = 8.0f;

        /// How far MUTATE's content travels as the face sinks.  Matches the
        /// press travel Widgets.cpp gives every other pill.
        constexpr float actionPressTravel = 0.8f;

        /// UI spec 6: the action-row divider runs y 598 -> 640 inside a row
        /// that spans 590 -> 647, i.e. inset 8 at each end.
        constexpr float dividerInset = 8.0f;

        /// The status line.  There is no constant for it because there is no
        /// status line in the reference image: it exists only because the
        /// mutation engine does not, and the buttons must not lie about what
        /// they did.  It sits in the dead band between the bottom of the
        /// action row (y 200) and the top of the preserve row (y 239), right
        /// aligned to the right edge of MAKE INSTRUMENT.
        constexpr float statusTop   = 216.0f;
        constexpr float statusSize   = 7.5f;
        constexpr float statusTrack  = 0.16f;
        constexpr float statusWidth  = 600.0f;

        /// Air above and below the 14 px preserve track.  Layout.h gives the
        /// row a text position and a track height but no band for the
        /// PreserveLock components to occupy.
        constexpr float preserveRowPad = 4.0f;

        /// Seed range.  Four digits keeps the status line short and the seed
        /// quotable ("SEED 4192"), and the engine only ever needs a stable
        /// integer to re-derive the same mutation from.
        constexpr int seedMin = 1000;
        constexpr int seedMax = 9999;

        /// The session tree is serialised into the host's project file, so the
        /// recipe log is bounded.  128 is far more than a session's worth of
        /// mutates and keeps the blob small.
        constexpr int maxHistoryEntries = 128;

        /// UI spec 6, in order: seven named holds and one unlabelled master.
        struct PreserveEntry { const char* caption; PID pid; };

        constexpr std::array<PreserveEntry, 8> preserveRow {{
            { "PITCH",      PID::preservePitch      },
            { "KEY",        PID::preserveKey        },
            { "RHYTHM",     PID::preserveRhythm     },
            { "TRANSIENTS", PID::preserveTransients },
            { "STEREO",     PID::preserveStereo     },
            { "LENGTH",     PID::preserveLength     },
            { "LOW END",    PID::preserveLowEnd     },
            { "",           PID::preserveAll        }   // the master switch
        }};

        // -------------------------------------------------------------------
        //  Every layout::mut::*Base constant is a true text baseline, and
        //  ui::ceramicLabel takes a baseline, so the two meet with no
        //  conversion.  The indirection is kept because the status line's own
        //  position is a local constant rather than a layout one, and it is
        //  worth both going through the same door.
        // -------------------------------------------------------------------
        float labelBaseline (float, float baseline)
        {
            return baseline;
        }

        /// Both the intent row and the preserve row run to the right edge of
        /// MAKE INSTRUMENT: it is the rightmost element in the panel and it is
        /// what sets the content column's right margin in the reference.
        float contentRight() noexcept
        {
            return mut::makeInstrument.getRight();
        }

        juce::String sep()  { return juce::String::fromUTF8 (" \xc2\xb7 "); }   // middot
        juce::String dash() { return juce::String::fromUTF8 (" \xe2\x80\x94 "); } // em dash

        juce::String choiceName (PID p, int index)
        {
            const auto choices = ParameterRegistry::choicesOf (p);
            return juce::isPositiveAndBelow (index, choices.size()) ? choices[index]
                                                                    : juce::String (index);
        }

        juce::String paramTip (PID p)
        {
            const auto& d = ParameterRegistry::definition (p);
            return juce::String (d.name) + "\n" + juce::String (d.tooltip);
        }
    }

    // =======================================================================
    //  MutateButton
    // =======================================================================
    MutateButton::MutateButton()
        : PillButton ("MUTATE", PillButton::Style::violet)
    {
    }

    void MutateButton::buttonStateChanged()
    {
        PillButton::buttonStateChanged();

        hoverAnim.setTarget (isOver() ? 1.0f : 0.0f);
        pressAnim.setTarget (isDown() ? 1.0f : 0.0f);
    }

    void MutateButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);

        // The booleans JUCE hands paintButton are the truth; the motions are
        // the same truth part-way there.
        hoverAnim.setTarget (highlighted ? 1.0f : 0.0f);
        pressAnim.setTarget (down ? 1.0f : 0.0f);

        const float press = pressAnim.get();
        const float hover = hoverAnim.get();

        // The body: exactly what PillButton::Style::violet draws, because that
        // part was already right.
        theme::accentSurface (g, b, actionCorner, theme::violet, press, hover);

        // The ink.  This was violetDeep, the darkest violet the palette had -
        // and measured on the render it gave a contrast ratio of 1.6:1 against
        // a face accentSurface brightens by 0.30.  That is unreadable, and it
        // was reported as a number rather than accepted as a look, which is
        // what got theme::violetInk added: the fourth violet, dark enough to
        // read at 4.4:1 and still unambiguously the same hue.
        auto colour = theme::violetInk;

        if (! isEnabled())
            colour = colour.withAlpha (0.45f);

        // Content travels with the face it is printed on.
        const float travel = press * actionPressTravel;

        const auto  font     = theme::label (mutateTextSize);
        const float iconSize = b.getHeight() * actionIconRatio;
        const float textW    = theme::trackedWidth (getButtonText(), font, mutateTextTrack);

        const float content = textW + iconSize + actionIconGap;

        float x = b.getCentreX() - content * 0.5f;

        icons::draw (g, icons::Icon::sparkle,
                     layout::centredSquare ({ x + iconSize * 0.5f, b.getCentreY() + travel },
                                            iconSize * 0.5f),
                     colour, glyphStroke);

        x += iconSize + actionIconGap;

        g.setColour (colour);
        theme::drawTracked (g, getButtonText(),
                            { x, b.getY() + travel, textW, b.getHeight() },
                            font, mutateTextTrack, juce::Justification::centredLeft);
    }

    // =======================================================================
    //  MutationPanel
    // =======================================================================
    MutationPanel::MutationPanel (NacarProcessor& p, EditorHost& h)
        : processor (p),
          host (h),
          // The options come from the parameter table, not from a retyped
          // literal, so segment index always equals choice index.
          harmonySelector  (ParameterRegistry::choicesOf (PID::harmonyMode),
                            SegmentedControl::Style::violetFill),
          distanceSelector (ParameterRegistry::choicesOf (PID::distanceMode),
                            SegmentedControl::Style::darkFill),
          intentSelector   (p.getParameters()),
          // MutateButton names and styles itself - see the class comment.
          againButton          ("AGAIN",           PillButton::Style::glass),
          printButton          ("PRINT",           PillButton::Style::glass),
          makeInstrumentButton ("MAKE INSTRUMENT", PillButton::Style::ceramic)
    {
        rng.setSeedRandomly();

        const auto& params = processor.getParameters();

        // -- Harmony and Distance -------------------------------------------
        //
        //  Two axes, two parameters, no coupling between them.  Harmony governs
        //  how far a mutation may stray harmonically; Distance governs how far
        //  it may stray timbrally.  The colour asymmetry (violet fill for
        //  Harmony, dark fill for Distance) is in the reference and is
        //  deliberate - it is what tells the two axes apart at a glance.
        harmonySelector.setTextSize (mut::selectorLabelSize, mut::selectorLabelTrack);
        harmonySelector.bindTo (params, PID::harmonyMode);
        harmonySelector.setTooltip (paramTip (PID::harmonyMode));
        addAndMakeVisible (harmonySelector);

        distanceSelector.setTextSize (mut::selectorLabelSize, mut::selectorLabelTrack);
        distanceSelector.bindTo (params, PID::distanceMode);
        distanceSelector.setTooltip (paramTip (PID::distanceMode));
        addAndMakeVisible (distanceSelector);

        // -- Intent ----------------------------------------------------------
        addAndMakeVisible (intentSelector);

        // -- Action row ------------------------------------------------------
        //
        //  MUTATE draws its own face, so it carries its own type, corner and
        //  glyph rather than being told them here; the three beside it are
        //  ordinary pills and are set up in full.
        mutateButton.setTooltip ("Mutate\nRolls a new mutation seed and records the recipe "
                                 "(intent, harmony, distance). The transformation engine "
                                 "arrives in phase 21 - no audio is altered yet.");
        mutateButton.onClick = [this] { runMutation (Trigger::mutate); };
        addAndMakeVisible (mutateButton);

        againButton.setTextSize (actionTextSize, actionTextTrack);
        againButton.setCornerRadius (actionCorner);
        againButton.setIcon (icons::Icon::cube, actionIconRatio);
        againButton.setTooltip ("Again\nRe-rolls the seed only and keeps the last recipe's "
                                "intent, harmony and distance. Same request, different dice.");
        againButton.onClick = [this] { runMutation (Trigger::again); };
        addAndMakeVisible (againButton);

        printButton.setTextSize (actionTextSize, actionTextTrack);
        printButton.setCornerRadius (actionCorner);
        printButton.setIcon (icons::Icon::print, actionIconRatio);
        printButton.setTooltip ("Print\nRenders the current mutation down to a new sample. "
                                "Not available yet - there is no render engine until phase 22.");
        printButton.onClick = [this]
        {
            // Deliberately inert on the audio side: there is nothing to render.
            setStatus ("PRINT" + dash() + "AWAITING RENDER ENGINE (PHASE 22)");
        };
        addAndMakeVisible (printButton);

        makeInstrumentButton.setTextSize (actionTextSize, actionTextTrack);
        makeInstrumentButton.setCornerRadius (actionCorner);
        makeInstrumentButton.setIcon (icons::Icon::makeInstrument, actionIconRatio);
        makeInstrumentButton.setTrailingIcon (icons::Icon::chevronRight);
        makeInstrumentButton.setTooltip ("Make Instrument\nBuilds a playable multisampled "
                                         "instrument from the mutation. Not available yet - "
                                         "it needs the mutation engine from phase 21.");
        makeInstrumentButton.onClick = [this]
        {
            setStatus ("MAKE INSTRUMENT" + dash() + "AWAITING THE INSTRUMENT BUILDER (PHASE 23)");
        };
        addAndMakeVisible (makeInstrumentButton);

        // -- Preserve row ----------------------------------------------------
        for (int i = 0; i < numPreserveLocks; ++i)
        {
            const auto& entry = preserveRow[(size_t) i];

            auto lock = std::make_unique<PreserveLock> (juce::String (entry.caption),
                                                        params, entry.pid);

            lock->getSwitch().setTooltip (paramTip (entry.pid));

            addAndMakeVisible (*lock);
            preserveLocks[(size_t) i] = std::move (lock);
        }

        // The master switch is the unlabelled one at the end of the row.
        if (auto* master = preserveLocks[(size_t) numPreserveLocks - 1].get())
        {
            master->getSwitch().setTooltip (
                "Preserve All\nThe master lock. Holds pitch, key, rhythm, transients, "
                "stereo, length and low end all at once.\n"
                "Switching it on locks all seven; switching it off leaves them exactly "
                "where they are.");

            // ToggleSwitch::bindTo already writes PID::preserveAll, so onToggle
            // is free for the extra behaviour and does not shadow the binding.
            master->getSwitch().onToggle = [this] (bool on) { applyMasterPreserve (on); };
        }

        // -- Opening status --------------------------------------------------
        //
        //  The panel states up front that the engine is not here, so the first
        //  thing a reviewer reads is the truth rather than a blank line.
        const int storedSeed = (int) mutationTree().getProperty (ids::currentSeed, 0);

        setStatus (storedSeed > 0
                       ? "SEED " + juce::String (storedSeed) + sep() + "READY"
                       : juce::String ("NO MUTATION YET") + sep() + "READY");
    }

    MutationPanel::~MutationPanel() = default;

    // -----------------------------------------------------------------------
    //  Painting
    // -----------------------------------------------------------------------
    void MutationPanel::paint (juce::Graphics& g)
    {
        const auto bounds = getLocalBounds().toFloat().reduced (shadowInset);

        // The panel is a machined plate sitting on the chassis, not a filled
        // rectangle: theme::raisedCeramic gives it the contact shadow, the
        // specular along its top edge, the bevel along its bottom one, and -
        // the detail UI spec section 12 singles out - the inset highlight one
        // pixel inside the top edge that reads as the material's own thickness.
        // At Elevation::resting, because a panel is seated, not floating.
        theme::raisedCeramic (g, bounds, layout::radiusPanel, theme::Elevation::resting);

        // -- heading ---------------------------------------------------------
        icons::draw (g, icons::Icon::sparkle,
                     layout::centredSquare (mut::sparkle, sparkleSize * 0.5f),
                     theme::violet, glyphStroke);

        {
            // The second-biggest type in the instrument after the wordmark, so
            // it is set in the display face rather than the small-caps label
            // face ui::ceramicLabel uses.
            const auto font = theme::display (mut::titleSize);

            // theme::drawTracked lays a run out from the TOP of its em box, and
            // mut::titleBase is a baseline, so the ascent comes off here
            // is the one place in the panel that needs no conversion.
            g.setColour (theme::ink);
            theme::drawTracked (g, "MUTATE",
                                { mut::titleX, mut::titleBase - font.getAscent(),
                                  titleRunWidth, font.getHeight() },
                                font, mut::titleTrack);
        }

        ceramicLabel (g, "TRANSFORM SOUND INTELLIGENTLY",
                      { mut::subtitleX, labelBaseline (mut::subtitleSize, mut::subtitleBase) },
                      mut::subtitleSize, mut::subtitleTrack, theme::inkFaint);

        // -- selector labels --------------------------------------------------
        const float selectorLabelY = labelBaseline (mut::selectorLabelSize, mut::selectorBase);

        ceramicLabel (g, "HARMONY", { mut::harmonyLabelX, selectorLabelY },
                      mut::selectorLabelSize, mut::selectorLabelTrack, theme::inkMuted);

        ceramicLabel (g, "DISTANCE", { mut::distanceLabelX, selectorLabelY },
                      mut::selectorLabelSize, mut::selectorLabelTrack, theme::inkMuted);

        // -- action row divider ------------------------------------------------
        theme::hairline (g,
                         { mut::actionDivider, mut::actionY + dividerInset },
                         { mut::actionDivider, mut::actionY + mut::actionH - dividerInset },
                         theme::ceramicEdge);

        // -- status line --------------------------------------------------------
        ceramicLabel (g, status,
                      { contentRight(), labelBaseline (statusSize, statusTop) },
                      statusSize, statusTrack, theme::inkFaint,
                      juce::Justification::right, statusWidth);

        // -- preserve row label ---------------------------------------------------
        ceramicLabel (g, "PRESERVE",
                      { mut::preserveLabelX, labelBaseline (mut::preserveSize, mut::preserveBase) },
                      mut::preserveSize, mut::preserveTrack, theme::inkMuted);
    }

    // -----------------------------------------------------------------------
    //  Layout
    // -----------------------------------------------------------------------
    void MutationPanel::resized()
    {
        harmonySelector .setBounds (mut::harmonySeg .toNearestInt());
        distanceSelector.setBounds (mut::distanceSeg.toNearestInt());

        intentSelector.setBounds (juce::Rectangle<float> (mut::intentX0, mut::intentY,
                                                          contentRight() - mut::intentX0,
                                                          mut::intentH).toNearestInt());

        mutateButton        .setBounds (mut::mutateButton .toNearestInt());
        againButton         .setBounds (mut::againButton  .toNearestInt());
        printButton         .setBounds (mut::printButton  .toNearestInt());
        makeInstrumentButton.setBounds (mut::makeInstrument.toNearestInt());

        // ------------------------------------------------------------------
        //  Preserve row - same one-line rule as the intent row.
        //
        //  Each lock is sized to itself; the only thing that may give is the
        //  gap between them, and it gives by the same amount everywhere.  The
        //  captions are fixed by the spec, so wrapping the row or clipping
        //  TRANSIENTS would both be worse than a tighter rhythm.
        // ------------------------------------------------------------------
        float lockWidths = 0.0f;

        for (const auto& lock : preserveLocks)
            if (lock != nullptr)
                lockWidths += lock->preferredWidth();

        const float available = contentRight() - mut::preserveX0;
        const float gapCount  = (float) (numPreserveLocks - 1);

        float gap = mut::preserveGap;

        if (lockWidths + gap * gapCount > available)
            gap = juce::jmax (0.0f, (available - lockWidths) / gapCount);

        // Layout.h gives the row a type position and a track height but no
        // band, so the band is derived: switch height plus air, centred on the
        // optical middle of the caption (half the em box below its top).
        const float rowH = mut::switchH + preserveRowPad * 2.0f;
        const float rowY = mut::preserveBase + mut::preserveSize * 0.5f - rowH * 0.5f;

        float x = mut::preserveX0;

        for (const auto& lock : preserveLocks)
        {
            if (lock == nullptr)
                continue;

            const float w = lock->preferredWidth();

            lock->setBounds (juce::Rectangle<float> (x, rowY, w, rowH).toNearestInt());
            x += w + gap;
        }
    }

    // -----------------------------------------------------------------------
    //  Session state
    // -----------------------------------------------------------------------
    juce::ValueTree MutationPanel::mutationTree()
    {
        return processor.getStateManager().group (ids::MUTATION);
    }

    juce::ValueTree MutationPanel::historyTree (juce::ValueTree& mutation)
    {
        auto history = mutation.getChildWithName (ids::HISTORY);

        if (! history.isValid())
        {
            // A session restored from a build that predates HISTORY.
            history = juce::ValueTree (ids::HISTORY);
            mutation.addChild (history, -1, nullptr);
        }

        return history;
    }

    // -----------------------------------------------------------------------
    //  MUTATE / AGAIN
    //
    //  This is the whole of the mutation wiring, and it is state only.  See
    //  the block at the top of the file.
    // -----------------------------------------------------------------------
    void MutationPanel::runMutation (Trigger trigger)
    {
        const auto& params = processor.getParameters();

        auto mutation = mutationTree();
        auto history  = historyTree (mutation);

        // -- the seed --------------------------------------------------------
        const bool locked = (bool) mutation.getProperty (ids::seedLocked, false);

        int seed = (int) mutation.getProperty (ids::currentSeed, 0);

        // A locked seed is held across both MUTATE and AGAIN - that is the
        // whole point of the lock.  The one exception is a session that has
        // never had a seed at all: there is nothing to hold, so one is made.
        if (! locked || seed <= 0)
        {
            seed = rng.nextInt ({ seedMin, seedMax + 1 });
            mutation.setProperty (ids::currentSeed, seed, nullptr);
        }

        // -- the recipe ------------------------------------------------------
        int intent   = params.choice (PID::mutationIntent);
        int harmony  = params.choice (PID::harmonyMode);
        int distance = params.choice (PID::distanceMode);

        if (trigger == Trigger::again && history.getNumChildren() > 0)
        {
            // AGAIN reuses everything except the seed: it asks the same
            // question again rather than asking whatever the controls happen to
            // say now.  With no history there is nothing to reuse, so it falls
            // through to the live controls.
            const auto last = history.getChild (history.getNumChildren() - 1);

            intent   = (int) last.getProperty (ids::recipeIntent,   intent);
            harmony  = (int) last.getProperty (ids::recipeHarmony,  harmony);
            distance = (int) last.getProperty (ids::recipeDistance, distance);
        }

        juce::ValueTree recipe (ids::RECIPE);
        recipe.setProperty (ids::recipeSeed,      seed,     nullptr);
        recipe.setProperty (ids::recipeIntent,    intent,   nullptr);
        recipe.setProperty (ids::recipeHarmony,   harmony,  nullptr);
        recipe.setProperty (ids::recipeDistance,  distance, nullptr);
        recipe.setProperty (ids::recipeScore,     0.0,      nullptr);   // engine rates this
        recipe.setProperty (ids::recipeFavourite, false,    nullptr);
        recipe.setProperty (ids::recipePayload,   "",       nullptr);   // engine fills this

        history.addChild (recipe, -1, nullptr);

        while (history.getNumChildren() > maxHistoryEntries)
            history.removeChild (0, nullptr);

        mutation.setProperty (ids::historyIndex, history.getNumChildren() - 1, nullptr);

        // -- the engine --------------------------------------------------------
        //
        // The recipe above is what gets stored; this is the same thing in the
        // engine's own type. It is built from the registry so that the seven
        // preserve locks come from the switches the user can see, and then the
        // four decisions this panel just made are written over the top - the
        // seed in particular, which may be a locked one rather than a fresh
        // roll.
        auto engineRecipe = mutation::Recipe::fromParameters (processor.getParameters());

        engineRecipe.seed        = (juce::uint32) seed;
        engineRecipe.intent      = (mutation::Intent) intent;
        engineRecipe.harmonyMode = (harmony::Mode) harmony;
        engineRecipe.distance    = (mutation::Distance) distance;

        juce::String failure;
        const bool started = processor.requestMutation (engineRecipe, failure);

        // -- feedback ---------------------------------------------------------
        juce::String s;
        s << "SEED " << seed;

        if (locked)
            s << " (LOCKED)";

        s << sep() << "INTENT " << choiceName (PID::mutationIntent, intent)
          << sep() << choiceName (PID::harmonyMode, harmony)
          << "/"   << choiceName (PID::distanceMode, distance)
          << sep() << (started ? juce::String ("RENDERING") : failure);

        setStatus (s);

        // The seed and the recipe log are chassis-wide state; anything else
        // showing them gets a chance to redraw.
        host.chassisChanged();
    }

    void MutationPanel::setStatus (juce::String newStatus)
    {
        status = std::move (newStatus);
        repaint();
    }

    // -----------------------------------------------------------------------
    //  Master preserve lock
    // -----------------------------------------------------------------------
    void MutationPanel::applyMasterPreserve (bool on)
    {
        // Switching the master ON locks all seven holds.  Switching it OFF
        // deliberately leaves them exactly where they are: a master that
        // silently released every lock the user had set by hand would throw
        // away a decision they made, and a switch has no undo of its own.  The
        // asymmetry is the safe direction.
        if (! on)
            return;

        const auto& params = processor.getParameters();

        for (int i = 0; i < numPreserveLocks - 1; ++i)
        {
            params.setFromUI (preserveRow[(size_t) i].pid, 1.0f);

            // The parameter write is the truth; this just stops the switch
            // lagging a poll behind it.
            if (auto* lock = preserveLocks[(size_t) i].get())
                lock->getSwitch().setToggleState (true, juce::dontSendNotification);
        }
    }
}
