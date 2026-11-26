// The `--on report` measurement tables.
//
// Split out of Setup.cpp because it is a different kind of code: setup plumbed the rig
// once, this walks it 32-64 times and prints what it measured. It reads only the
// finished Character, so it takes nothing but that.
//
// NOTE: names gldx types, so include AFTER `import gldx;`.
#pragma once

#include "Character.h"

namespace anim {

/**
 * Dump the gait/pose measurement tables once.
 *
 * @param traceAction action whose timeline `--on trace` walks; may be empty
 */
void ReportGait(Character& character, const char* traceAction);

}  // namespace anim
