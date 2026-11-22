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
    // NOT CURRENTLY USED. Every action now takes the engine template (see TuningsFor
    // below), because the task is to design ONE good template on the clean rig rather
    // than keep two sets of numbers in sync. A rig that cannot take the reference
    // amplitudes needs its bind fixed, and a second tuning hides that.
    //
    // Kept because the derived structs document WHAT breaks on a coarse-weight rig and
    // why (hem vertices welded to one thigh bone, no blend at the knee, sleeve against
    // torso). That is the argument for "fix the bind, not the animation", and deleting
    // it would delete the evidence.
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
