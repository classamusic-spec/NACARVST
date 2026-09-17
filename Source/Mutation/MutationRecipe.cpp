#include "MutationRecipe.h"
#include "MutationEngine.h"

#include "../Plugin/ParameterRegistry.h"
#include "../Plugin/StateManager.h"

namespace nacar::mutation
{
    // =======================================================================
    //  Names
    // =======================================================================
    const char* nameOf (Intent intent) noexcept
    {
        switch (intent)
        {
            case Intent::memory:    return "MEMORY";
            case Intent::cloud:     return "CLOUD";
            case Intent::broken:    return "BROKEN";
            case Intent::reverse:   return "REVERSE";
            case Intent::distant:   return "DISTANT";
            case Intent::rhythmic:  return "RHYTHMIC";
            case Intent::dark:      return "DARK";
            case Intent::ghost:     return "GHOST";
            case Intent::playable:  return "PLAYABLE";
            case Intent::cinematic: return "CINEMATIC";
            case Intent::count:
            default:                break;
        }

        return "MEMORY";
    }

    // =======================================================================
    //  Preserve
    // =======================================================================
    Preserve Preserve::all() noexcept
    {
        Preserve p;

        p.pitch = p.key = p.rhythm = p.transients = true;
        p.stereo = p.length = p.lowEnd = true;

        return p;
    }

    bool Preserve::any() const noexcept
    {
        return pitch || key || rhythm || transients || stereo || length || lowEnd;
    }

    // =======================================================================
    //  Serialisation
    //
    //  THE PROPERTY NAMES ARE NOT NEW.  `StateManager` reserves the four the
    //  MUTATE panel already writes - recipeSeed, recipeIntent, recipeHarmony,
    //  recipeDistance - plus engineVersion, and this writes exactly those.  The
    //  seven locks have no reserved RECIPE identifier, so they are stored under
    //  the seven parameters' own permanent string IDs from `ParameterList.h`.
    //  Those IDs are already permanent and already mean exactly this, so no new
    //  identifier enters the session tree.
    //
    //  A recipe written by the panel before this engine existed carries none of
    //  the locks and no version.  `readFrom` reads it as "nothing locked,
    //  version 1", which is what those sessions meant.
    // =======================================================================
    namespace
    {
        /** The seven locks, in the fixed order they are written and read.
            `-Wswitch-enum` is on for this build and is right to be, so this
            indexes rather than switching over PID. */
        enum Lock { pitchLock = 0, keyLock, rhythmLock, transientLock,
                    stereoLock, lengthLock, lowEndLock, numLocks };

        const juce::Identifier& lockId (int lock)
        {
            static const juce::Identifier identifiers[(size_t) numLocks]
            {
                juce::Identifier (ParameterRegistry::idOf (PID::preservePitch)),
                juce::Identifier (ParameterRegistry::idOf (PID::preserveKey)),
                juce::Identifier (ParameterRegistry::idOf (PID::preserveRhythm)),
                juce::Identifier (ParameterRegistry::idOf (PID::preserveTransients)),
                juce::Identifier (ParameterRegistry::idOf (PID::preserveStereo)),
                juce::Identifier (ParameterRegistry::idOf (PID::preserveLength)),
                juce::Identifier (ParameterRegistry::idOf (PID::preserveLowEnd))
            };

            return identifiers[(size_t) juce::jlimit (0, (int) numLocks - 1, lock)];
        }
    }

    void Recipe::writeTo (juce::ValueTree& node, juce::UndoManager* undo) const
    {
        if (! node.isValid())
            return;

        // The seed is stored as a 64-bit integer so that the full 32-bit range
        // round-trips: the panel rolls four digits, but a recipe that arrives
        // from anywhere else must not lose its top bit.
        node.setProperty (ids::recipeSeed,     (juce::int64) seed,        undo);
        node.setProperty (ids::recipeIntent,   (int) intent,              undo);
        node.setProperty (ids::recipeHarmony,  (int) harmonyMode,         undo);
        node.setProperty (ids::recipeDistance, (int) distance,            undo);
        node.setProperty (ids::engineVersion,  engineVersion,             undo);

        node.setProperty (lockId (pitchLock),     preserve.pitch,      undo);
        node.setProperty (lockId (keyLock),       preserve.key,        undo);
        node.setProperty (lockId (rhythmLock),    preserve.rhythm,     undo);
        node.setProperty (lockId (transientLock), preserve.transients, undo);
        node.setProperty (lockId (stereoLock),    preserve.stereo,     undo);
        node.setProperty (lockId (lengthLock),    preserve.length,     undo);
        node.setProperty (lockId (lowEndLock),    preserve.lowEnd,     undo);
    }

    Recipe Recipe::readFrom (const juce::ValueTree& node)
    {
        Recipe r;

        if (! node.isValid())
            return r;

        r.seed = (juce::uint32) (juce::int64) node.getProperty (ids::recipeSeed, 0);

        r.intent = (Intent) juce::jlimit (0, (int) Intent::count - 1,
                                          (int) node.getProperty (ids::recipeIntent, 0));

        r.harmonyMode = (harmony::Mode) juce::jlimit (0, 2,
                                                      (int) node.getProperty (ids::recipeHarmony, 0));

        r.distance = (Distance) juce::jlimit (0, 2,
                                              (int) node.getProperty (ids::recipeDistance, 0));

        r.engineVersion = (int) node.getProperty (ids::engineVersion, 1);

        r.preserve.pitch      = (bool) node.getProperty (lockId (pitchLock), false);
        r.preserve.key        = (bool) node.getProperty (lockId (keyLock), false);
        r.preserve.rhythm     = (bool) node.getProperty (lockId (rhythmLock), false);
        r.preserve.transients = (bool) node.getProperty (lockId (transientLock), false);
        r.preserve.stereo     = (bool) node.getProperty (lockId (stereoLock), false);
        r.preserve.length     = (bool) node.getProperty (lockId (lengthLock), false);
        r.preserve.lowEnd     = (bool) node.getProperty (lockId (lowEndLock), false);

        return r;
    }

    Recipe Recipe::fromParameters (const ParameterRegistry& params)
    {
        Recipe r;

        // userValue, not raw: a recipe records what the user chose.  If the
        // modulation matrix is ever pointed at one of these, the knob's own
        // position is still what the mutation was asked for.
        const auto choiceOf = [&params] (PID p)
        {
            return juce::jmax (0, (int) params.userValue (p));
        };

        const auto flagOf = [&params] (PID p)
        {
            return params.userValue (p) > 0.5f;
        };

        r.intent = (Intent) juce::jlimit (0, (int) Intent::count - 1,
                                          choiceOf (PID::mutationIntent));

        r.harmonyMode = (harmony::Mode) juce::jlimit (0, 2, choiceOf (PID::harmonyMode));
        r.distance    = (Distance)      juce::jlimit (0, 2, choiceOf (PID::distanceMode));

        if (flagOf (PID::preserveAll))
        {
            r.preserve = Preserve::all();
        }
        else
        {
            r.preserve.pitch      = flagOf (PID::preservePitch);
            r.preserve.key        = flagOf (PID::preserveKey);
            r.preserve.rhythm     = flagOf (PID::preserveRhythm);
            r.preserve.transients = flagOf (PID::preserveTransients);
            r.preserve.stereo     = flagOf (PID::preserveStereo);
            r.preserve.length     = flagOf (PID::preserveLength);
            r.preserve.lowEnd     = flagOf (PID::preserveLowEnd);
        }

        r.engineVersion = currentEngineVersion;

        return r;
    }
}
