#!/bin/bash
# msvc-object-probe.sh - Pin down what MSVC's C1605 actually runs out of.
# Usage: ./scripts/msvc-object-probe.sh [build-dir]
#
# Context: since v1.7.0 the Windows job dies with
#   fatal error C1605: compiler limit: object file size cannot exceed 4 GB
# on exactly two translation units (src/demo/glass_water/Hud.cpp and
# src/demo/night_forest/Hud.cpp). Three explanations were already falsified by
# CI: "/bigobj raises the 4 GB ceiling, so the section table was exhausted"
# (run 42760865230 shows /bigobj on the cl command line with the byte-identical
# error), "the TU is simply too big" (night_forest/main.cpp and
# glass_water/main.cpp are larger, import gldx and include the same Hud.h, and
# compile fine), and "importing several modules is what does it" (counting the
# C5050 lines by the source path each one carries: 18 demo sources import gldxwin,
# many of them gldx + gldxwin + gldxcli, while the build fails on exactly two
# objects). clang has no equivalent limit, so no local build can observe any of
# this; a full Windows build costs ~15 minutes.
#
# The first version of this script produced nothing but `rc=127` (run
# 42762856818): the command line ninja hands back starts with
# C:\PROGRA~1\...\cl.exe, and Git Bash eats those backslashes as escapes, so
# the compiler was never invoked. Everything below therefore goes through
# replay_cl(), which normalises the whole command line to forward slashes and
# switches off MSYS argument conversion, and prints the log whenever a variant
# does not even reach a compiler error - a probe that fails silently is worse
# than no probe.
#
# For each failing TU the script re-runs the original compile as a variant set:
#   baseline       unchanged, must reproduce C1605 or the whole matrix is void
#   no-bigobj      if the message becomes C1128 the limit really is section
#                  count and /bigobj is not taking effect on this path
#   /Ob1           fewer inline function bodies -> fewer COMDAT sections
#   /Os            same pressure from the size-directed optimiser instead
#   /Zc:inline-    the opposite direction: keep unreferenced inline bodies in
#                  the object, which should make a section problem worse
#   -D_UTF8        the demo TU is compiled without /utf-8 while gldx* is built
#                  with it (that is what C5050 complains about every single
#                  frame of this build); matching the macro tests whether the
#                  module environment mismatch is on the path at all
#   skeleton TU    the same preamble (Platform.h + import gldx + Hud.h + STL +
#                  glm) with an empty body, which separates "any importer of
#                  this interface hits the wall" from "something in these two
#                  bodies does"
# Then, for glass_water/Hud.cpp only, it walks the real file in prefixes that
# end at statement boundaries, with the open scopes closed synthetically, which
# localises the construct that trips the object writer instead of describing it
# again. Cuts that stop parsing are reported as such, so an inconclusive row
# cannot be read as a pass. The whole probe is time-bounded.
#
# The script never fails the build: it is meant to run from a step guarded by
# `if: failure()`.

set -uo pipefail
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

BUILD_DIR="${1:-build}"
TIME_BUDGET="${PROBE_TIME_BUDGET:-600}"   # seconds spent on the prefix walk
MaxCuts="${PROBE_MAX_CUTS:-16}"           # probes in the prefix walk
if [ ! -d "$BUILD_DIR" ]; then
    echo "no build directory at $BUILD_DIR" >&2
    exit 0
fi
cd "$BUILD_DIR" || exit 0

Demos="glass_water night_forest"
BisectDemo="glass_water"

# replay_cl <exe-and-flags> <out-obj> <source> <log>  -> cl's exit code
replay_cl() {
    local cmd="$1 /Fo$2 /FS -c $3"
    cmd="${cmd//\\//}"      # Git Bash cannot replay backslash paths
    eval "$cmd" >"$4" 2>&1
}

# objsize <file> - bytes, or '?'; GNU and BSD stat spell the same thing differently
objsize() {
    stat -c %s "$1" 2>/dev/null || stat -f %z "$1" 2>/dev/null || echo '?'
}

# report <label> <rc> <log> [obj]
report() {
    local label="$1" rc="$2" log="$3" obj="${4:-}" codes msg
    codes="$(grep -oE 'error [A-Z][0-9]+' "$log" 2>/dev/null |
             awk '{printf "%s ", $2}')"
    msg="$(grep -m1 -oE 'error [A-Z][0-9]+.*' "$log" 2>/dev/null | cut -c1-96)"
    printf '  %-14s rc=%-4s %-14s %s\n' "$label" "$rc" "${codes:--}" "$msg"
    if [ "$rc" -ge 126 ] || { [ "$rc" -ne 0 ] && [ -z "$codes" ]; }; then
        printf '  %s !! no compiler error in the log, first lines:\n' ""
        head -3 "$log" 2>/dev/null | cut -c1-160 | sed 's/^/     /'
    fi
    if [ "$rc" -eq 0 ] && [ -n "$obj" ]; then
        local sections
        sections="$(dumpbin /headers "$obj" 2>/dev/null |
                    grep -m1 -oE '[0-9]+ sections' || echo '?')"
        printf '  %s -> %s bytes, %s\n' "" "$(objsize "$obj")" "$sections"
    fi
}

# run_variant <label> <prefix> <obj> <src>
run_variant() {
    local rc
    replay_cl "$2" "$3" "$4" "$3.log"; rc=$?
    report "$1" "$rc" "$3.log" "$3"
}

# prefix_walk <demo> <prefix> - does C1605 survive a TU that stops halfway
# through Build()? Cuts that stop parsing are reported as such, so an
# inconclusive row cannot be read as a pass.
prefix_walk() {
    local demo="$1" prefix="$2"
    local full="../src/demo/${demo}/Hud.cpp"
    echo "================ prefix walk: ${demo}/Hud.cpp ================"
    if [ ! -f "$full" ]; then
        echo "  $full not found, skipping the walk"
        return 0
    fi
    mkdir -p probe_src
    # Candidate cut points are statement-terminating lines whose open scopes can
    # be closed synthetically (1..3 of them). Braces inside strings and comments
    # are counted as well, so a cut that turns out to be invalid simply reports
    # its own syntax error rather than being mistaken for a pass.
    local cands n stride i=0 cut opens rc text probe verdict deadline
    cands="$(awk '{
        for (i = 1; i <= length($0); i++) {
            c = substr($0, i, 1)
            if (c == "{") b++
            else if (c == "}") b--
        }
        line = $0
        sub(/[ \t\r]+$/, "", line)
        last = substr(line, length(line), 1)
        if ((last == ";" || last == "}") && b >= 1 && b <= 3) printf "%d %d\n", NR, b
    }' "$full")"
    n="$(printf '%s\n' "$cands" | grep -c .)"
    stride=$(( (n + MaxCuts - 1) / MaxCuts ))
    [ "$stride" -lt 1 ] && stride=1
    echo "  $n usable cut points, every ${stride}th one (${MaxCuts} probes max, ${TIME_BUDGET}s budget)"
    deadline=$((SECONDS + TIME_BUDGET))
    while read -r cut opens; do
        [ -n "$cut" ] || continue
        i=$((i + 1))
        [ $(( (i - 1) % stride )) -eq 0 ] || continue
        if [ "$SECONDS" -gt "$deadline" ]; then
            echo "  time budget reached at candidate $i/$n ($cut) - walk stops here"
            break
        fi
        text="$(sed -n "${cut}p" "$full" | sed 's/^[[:space:]]*//' | cut -c1-44)"
        probe="probe_src/cut_${demo}_${cut}.cpp"
        head -n "$cut" "$full" > "$probe"
        printf '%*s' "$opens" '' | tr ' ' '}' >> "$probe"
        replay_cl "$prefix" "probe_cut.obj" "$probe" "probe_cut.log"; rc=$?
        if grep -q 'error C1605' probe_cut.log; then
            verdict="C1605"
        elif [ "$rc" -ne 0 ]; then
            verdict="other: $(grep -m1 -oE 'error [A-Z][0-9]+' probe_cut.log | awk '{print $2}')"
        else
            verdict="compiled $(objsize probe_cut.obj) bytes"
        fi
        printf '  L%-4d (+%d)   %-30s %s\n' "$cut" "$opens" "$verdict" "$text"
    done <<< "$cands"
    rm -f probe_src/cut_${demo}_*.cpp probe_cut.obj probe_cut.log
}

for demo in $Demos; do
    echo "================ ${demo}/Hud.cpp ================"
    obj="src/demo/CMakeFiles/d_${demo}.dir/${demo}/Hud.cpp.obj"
    cmd="$(ninja -t commands "$obj" 2>/dev/null | tail -1)"
    if [ -z "$cmd" ]; then
        echo "  no ninja command found for $obj, skipping"
        continue
    fi
    cmd="${cmd//\\//}"
    src="${cmd##* -c }"
    # Keep everything up to the first /Fo; the object path and the source are
    # re-attached per variant.
    prefix="${cmd%% /Fo*}"
    echo "  compiler: ${prefix%% *} (exists: $([ -f "${prefix%% *}" ] && echo yes || echo no))"
    echo "  source:   $src"
    echo "  modmap:   $(ls -1 "${obj}.modmap" 2>/dev/null || echo '(none)')"

    run_variant "baseline"    "$prefix"                     "probe_${demo}_base.obj"  "$src"
    run_variant "no-bigobj"   "${prefix// \/bigobj/}"       "probe_${demo}_nobig.obj" "$src"
    run_variant "/Ob1"        "$prefix /Ob1"                "probe_${demo}_ob1.obj"   "$src"
    run_variant "/Os"         "$prefix /Os"                 "probe_${demo}_os.obj"    "$src"
    run_variant "/Zc:inline-" "$prefix /Zc:inline-"         "probe_${demo}_zci.obj"   "$src"
    run_variant "-D_UTF8"     "$prefix -D_UTF8"             "probe_${demo}_utf8.obj"  "$src"

    # Skeleton importer: same header/module preamble, no panel code at all. It
    # sits in the build tree; the demo directory is already on the include
    # path, so `#include "Hud.h"` still resolves the way it does for the real TU.
    skel="probe_src/skel_${demo}.cpp"
    mkdir -p probe_src
    cat >"$skel" <<'PROBE_EOF'
// Generated by scripts/msvc-object-probe.sh; not part of any target.
#include "gldx/core/Platform.h"

import gldx;

#include "Hud.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>

void msvc_probe_skeleton() {}
PROBE_EOF
    run_variant "skeleton"    "$prefix"                     "probe_${demo}_skel.obj"  "$skel"
    rm -f "$skel"

    if [ "$demo" = "$BisectDemo" ]; then
        prefix_walk "$demo" "$prefix"
    fi
done

echo "probe complete (${SECONDS}s total)"
exit 0
