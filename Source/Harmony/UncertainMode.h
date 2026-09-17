#pragma once

#include "Harmony.h"

/*
    KNOWING THE ROOT IS NOT KNOWING THE MODE.

    `AnalysisResult` asks these as two questions - keyIsUsable() and
    scaleIsUsable() - and `Context` answers them as two states.  A context
    built on a confident root and an unconfident mode still constrains, but it
    permits BOTH thirds rather than choosing one, because the third is the
    degree that decides whether a key is major or minor and snapping the wrong
    way there is the single most audible way to be wrong about a key.

    Harmony.h is frozen and `Context` has no field for this, so the state rides
    in the `scale` field: a value of `Scale::count + s` is scale s, believed but
    not confirmed.  maskOf() and nameOf() are total over that range - maskOf
    returns the widened set, nameOf the name of the scale behind it - so
    nothing that goes through the harmony API can encounter a value it does not
    understand, and snap(), snapCents() and permits() need no special case at
    the call site.

    These three declarations exist so that phase 21 can *ask*, rather than
    having to know the trick.  Anything that only moves pitches can ignore this
    header entirely.

    (If Harmony.h is ever unfrozen, the honest version of this is a
    `bool scaleKnown` next to `keyKnown`, and these functions become one-liners
    over it without changing a single call site.)
*/
namespace nacar::harmony
{
    /** True when a context knows its tonic but not its mode. Always false when
        the key itself is unknown - that is a different and wider state, in
        which nothing is constrained at all. */
    bool modeIsUncertain (const Context&) noexcept;

    /** The scale a context is built on, with any uncertainty stripped off, so
        it is always one of the nine and is always safe to index with. */
    Scale scaleOf (const Context&) noexcept;

    /** Marks a scale as a candidate rather than a conclusion. Idempotent. */
    Scale uncertainMode (Scale) noexcept;
}
