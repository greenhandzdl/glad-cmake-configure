// Loading and rig setup: everything that turns a decoded file into a drawable Character.
//
// Kept apart from main.cpp because it is one long data-plumbing function with no
// per-frame logic in it - the frame loop never calls into this, it only reads what this
// produced.
//
// NOTE: names gldx types, so include AFTER `import gldx;`.
#pragma once

#include "Character.h"

namespace anim {

/**
 * Take ownership of a decoded model, build the rig and every derived quantity.
 *
 * @param reportGait  dump the measurement tables once (from `--on report`)
 * @param traceAction which action the `--on trace` timeline walks, may be empty
 */
void UploadCharacter(Character& character, gldx::LoadedSkinnedModelData&& data,
                     bool reportGait, const char* traceAction);

}  // namespace anim
