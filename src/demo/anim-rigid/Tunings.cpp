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
    PoseTunings t;

    // What "coarse weights" actually breaks, and the one knob that fixes each:
    //
    //  · Hem vertices weighted entirely to one thigh bone. Any hip swing drags them as
    //    a rigid triangle out of the skirt cone, which reads as a tear. `hipSwing` is
    //    the direct multiplier on that displacement, so it comes down first.
    //  · Knee flex decides how far the shin leaves the thigh. With no transition
    //    weights around the knee, a large flex creases the surface; a smaller one keeps
    //    the silhouette continuous.
    //  · Arm swing drags the sleeve against the torso; there is no blend between the
    //    sleeve and the body mesh.
    //
    // Only amplitudes change. Stance ratio, cycle phase and the hip/knee/ankle
    // relationship are untouched, so this still reads as the same walk - just a
    // smaller one.
    t.walk.hipSwing       = 0.22f;   // was 0.33
    t.walk.kneeSwingFlex  = 0.52f;   // was 0.72
    t.walk.armSwing       = 0.26f;   // was 0.38
    t.walk.hipYaw         = 0.06f;   // was 0.10 - pelvis rotation shears the hem
    t.walk.chestYaw       = 0.09f;   // was 0.15

    t.run.hipSwing        = 0.30f;
    t.run.kneeSwingFlex   = 0.62f;

    t.idle.breathChest    = 0.010f;  // was 0.018 - torso weights are lumpy
    t.idle.swayAmplitude  = 0.007f;  // was 0.010

    return t;
}

PoseTunings TuningsFor(NamingChannelForTunings channel) {
    return (channel == NamingChannelForTunings::Coarse) ? CoarseSkinnedTunings()
                                                        : RigidTunings();
}

}  // namespace animrigid
