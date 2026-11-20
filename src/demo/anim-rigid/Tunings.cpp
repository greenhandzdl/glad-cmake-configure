// Module first, then the header that uses its types.
import gldx;

#include "Tunings.h"

namespace animrigid {

PoseTunings RigidTunings() {
    // The engine defaults were tuned against a well-weighted skirt character, which is
    // the same class of asset as the MMD one (per-panel skirt bones, separate finger
    // and hair chains), so nothing to override.
    return PoseTunings{};
}

PoseTunings CoarseSkinnedTunings() {
    // Inherit the engine's MMD template and override only the amplitudes this rig
    // cannot take. The derived structs (CoarseWalkTuning & co. in Tunings.h) hold the
    // overrides next to the reason for each one, so "what did we change for this
    // asset" is a diff rather than a whole copy.
    PoseTunings t;
    t.walk = CoarseWalkTuning{};
    t.run  = CoarseRunTuning{};
    t.idle = CoarseIdleTuning{};
    return t;
}

PoseTunings TuningsFor(NamingChannelForTunings channel) {
    return (channel == NamingChannelForTunings::Coarse) ? CoarseSkinnedTunings()
                                                        : RigidTunings();
}

}  // namespace animrigid
