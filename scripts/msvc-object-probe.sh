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
#
# What that matrix already settled (run 42765315428, master 40e63a3):
#   - all six flag variants reproduce the byte-identical error, so nothing on
#     that axis is involved;
#   - the skeleton TU compiles into ~1.5 KB, so importing gldx is not the wall;
#   - prefixes of glass_water/Hud.cpp pass up to L85 and fail from L97 onward,
#     and *every* failing prefix takes 11.1 s while every passing one takes
#     ~1.2 s. Time that does not grow with the amount of code is not the shape
#     of a size limit: once the trigger is present the writer runs to the wall.
#
# So the last round stopped sampling and closed the case (run 42770837956, master
# 541d66f). What came back:
#   bisect        both TUs converge on the SAME statement, `hud.AddLine(buf, kDim);`
#                 - glass_water fails at L86 with L85 passing, night_forest at L57
#                 with L56 passing;
#   /Od /Gw /GS-  all three still C1605, so the optimiser axis is dead as well; with
#                 the flag axis that is every compiler-side knob tried so far;
#   /Fa listing   285 bytes and 12 lines inside the watchdog's 3 s, and those lines
#                 are the prologue (`include listing.inc`, `_BSS       SEGMENT`). The
#                 compile dies before emitting a single instruction, so nothing is
#                 being *fed* to the writer: the writer itself is what runs away;
#   isolated      came back `other:C2065`, which is the probe's fault rather than
#                 MSVC's - with the rest of the body deleted, `buf` and `kDim` are no
#                 longer in scope. "Is it this statement alone?" therefore stays
#                 open, and deleting the body can never answer it. Only rewriting it
#                 can, which is what the matrix below now does:
#   substitution matrix
#                 the complete TU with ONLY the culprit region rewritten, one row per
#                 property of that region: deleted (the control - the rest of the file
#                 then has to compile), unchanged (the replay), the imported constant
#                 instead of the file's constexpr alias, a prvalue of the alias, a
#                 function-local non-constexpr copy, a function-local const reference,
#                 and a string literal instead of the char array that has to convert
#                 to std::string_view. The aliases and the buffer are read out of the
#                 source (alias_decls), so the rows follow whatever the culprit turns
#                 out to be instead of being written for this one line, and an axis
#                 that does not apply is printed as such rather than left silent.
#                 Why these particular properties: the two TUs that fail both declare
#                 `constexpr glm::vec4 kDim = Hud::kDim;` and pass the *local* name,
#                 while text_hud/main.cpp - which compiles - passes `Hud::kDim`
#                 directly and voxel_terrain/Hud.cpp declares no alias at all. That is
#                 still only an observation about which files fail; a row that compiles
#                 is what would turn it into a fact about which form is unloadable.
# Each row costs one whole-TU compile (~11 s), so the matrix is under two minutes per
# TU on top of the bisect.
#
# All of it except cl.exe itself is plain shell, and it is checked locally:
# scripts/msvc-object-probe-selftest.sh puts a fake `cl.exe` on PATH (C1605 above a
# chosen source length, an endless /Fa listing on a failing source, which is how the
# watchdog gets tested), points the probe at the real src/demo tree, and asserts that
# every row carries a verdict, that both bisections converge, that the matrix either
# answers or says why it cannot, that no intermediate file outlives the step - and
# that no *process* does. That last assertion is not decoration: the first version of
# the watchdog used `timeout`, whose signal reaches only the shell wrapper, so cl kept
# appending to a listing in a directory the selftest had already deleted. Ten such
# orphans from earlier rounds later filled this machine's disk and made `mktemp` fail,
# invisibly, because their files were unlinked but still open. kill_tree is what stops
# a writer; the process check is what stops kill_tree from regressing quietly.
#
# The script never fails the build: it is meant to run from a step guarded by
# `if: failure()`.

set -uo pipefail
export MSYS_NO_PATHCONV=1
export MSYS2_ARG_CONV_EXCL='*'

BUILD_DIR="${1:-build}"
TIME_BUDGET="${PROBE_TIME_BUDGET:-600}"   # seconds spent per bisect
ASM_BUDGET="${PROBE_ASM_BUDGET:-3}"       # seconds of /Fa listing before the watchdog
                                          # (~1 GB at the observed write rate)
if [ ! -d "$BUILD_DIR" ]; then
    echo "no build directory at $BUILD_DIR" >&2
    exit 0
fi
cd "$BUILD_DIR" || exit 0

Demos="glass_water night_forest"

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

# cut_points <file> - candidate statement-boundary cuts, as "line opens" pairs:
# statement-terminating lines whose still-open scopes (1..3) can be closed
# synthetically. Braces inside strings and comments are counted as well, so a
# cut that turns out to be invalid reports its own syntax error rather than
# being mistaken for a pass - the bisect below steps over those.
cut_points() {
    awk '{
        for (i = 1; i <= length($0); i++) {
            c = substr($0, i, 1)
            if (c == "{") b++
            else if (c == "}") b--
        }
        line = $0
        sub(/[ \t\r]+$/, "", line)
        last = substr(line, length(line), 1)
        if ((last == ";" || last == "}") && b >= 1 && b <= 3) printf "%d %d\n", NR, b
    }' "$1"
}

# cut_write <file> <line> <opens> <out> - the first <line> lines, closed out
cut_write() {
    head -n "$2" "$1" > "$4"
    printf '%*s' "$3" '' | tr ' ' '}' >> "$4"
}

# verdict_of <log> <rc> [obj] - exactly three kinds of answer, so an
# inconclusive row can never be read as a pass
verdict_of() {
    local log="$1" rc="$2" obj="${3:-}"
    if grep -q 'error C1605' "$log"; then
        echo "C1605"
    elif [ "$rc" -ne 0 ]; then
        echo "other:$(grep -m1 -oE 'error [A-Z][0-9]+' "$log" | awk '{print $2}')"
    elif [ -n "$obj" ]; then
        echo "pass $(objsize "$obj")B"
    else
        echo "pass"
    fi
}

# cut_probe <prefix> <file> <line> <opens> <objbase> - sets PV and RC
cut_probe() {
    cut_write "$2" "$3" "$4" "probe_cut.cpp"
    replay_cl "$1" "$5" "probe_cut.cpp" "$5.log"; RC=$?
    PV="$(verdict_of "$5.log" "$RC" "$5")"
}

# source_line <file> <line> - one line, trimmed and clipped for the log
source_line() {
    sed -n "$2p" "$1" | sed 's/^[[:space:]]*//' | cut -c1-44
}

# kill_tree <pid> - stop a runaway writer and everything it spawned.
#
# Killing the shell that was used to launch cl is not enough: cl is that shell's
# child, and it keeps writing. On Windows `taskkill /F /T` does the whole tree; on
# any other platform (which is where the selftest runs) the same thing has to be
# done by hand, recursion from the leaves up, or the local test would be proving a
# weaker property than the runner relies on.
kill_tree() {
    local pid="$1" kid
    if command -v taskkill >/dev/null 2>&1; then
        taskkill //F //T //PID "$pid" >/dev/null 2>&1
        return 0
    fi
    for kid in $(pgrep -P "$pid" 2>/dev/null); do kill_tree "$kid"; done
    kill -9 "$pid" 2>/dev/null
}

# listing_probe <prefix> <source> - ask the compiler what it is emitting. /Fa
# writes the listing as it goes, so a runaway shows up as a growing file.
#
# The watchdog is done by hand rather than with `timeout`, and it kills the whole
# process tree: `timeout` signals the shell wrapper, while cl.exe is that
# wrapper's child. A local selftest (scripts/msvc-object-probe-selftest.sh) caught
# exactly this - the killed writer kept appending, so the listing was still there
# and still growing after the probe reported itself finished. On a runner whose
# disk the build already used, a surviving 4 GB/s-capable writer is a worse
# problem than the bug being investigated. The rate implied by the observed
# failure is 4 GB in 11.1 s, so ASM_BUDGET seconds costs about that many GB.
listing_probe() {
    local prefix="$1" src="$2" asm="probe_listing.asm" log="probe_listing.log"
    local cmd="probe_listing_cmd.sh" rc waited=0 sz lines
    rm -f "$asm" probe_listing.obj
    printf '%s /Fa%s /Foprobe_listing.obj /FS -c %s\n' "$prefix" "$asm" "$src" > "$cmd"
    bash "$cmd" >"$log" 2>&1 &
    local wpid=$!
    while [ "$waited" -lt "$ASM_BUDGET" ] && kill -0 "$wpid" 2>/dev/null; do
        sleep 1; waited=$((waited + 1))
    done
    if kill -0 "$wpid" 2>/dev/null; then
        kill_tree "$wpid"
        wait "$wpid" 2>/dev/null
        rc=124
    else
        wait "$wpid"; rc=$?
    fi
    sz="$(objsize "$asm")"; lines="$(wc -l < "$asm" 2>/dev/null | tr -d ' ')"
    printf '  listing       rc=%-4s (%ss watchdog, 124 = killed) %s bytes, %s lines\n' \
        "$rc" "$ASM_BUDGET" "$sz" "${lines:-?}"
    if [ -s "$asm" ]; then
        echo "     most frequent listing lines (of the first 200k):"
        head -200000 "$asm" | sort | uniq -c | sort -rn | head -3 |
            cut -c1-120 | sed 's/^/     /'
    fi
    grep -oE 'error [A-Z][0-9]+' "$log" 2>/dev/null | head -3 | sed 's/^/     /'
    rm -f "$asm" probe_listing.obj "$cmd" "$log"
    # One more sweep after the kill has been reaped, so a writer that flushed a
    # final buffer between the check and the delete cannot leave the file behind.
    sleep 1
    rm -f "$asm"
}

# alias_decls <file> - the constexpr aliases this TU builds out of imported
# constants, as "localName type importedInitializer". The culprit statement uses
# the local name, while the importers that compile fine name the imported
# constant directly, so this map lets the substitution matrix be derived from the
# source instead of hardcoded to the line that happened to trip.
alias_decls() {
    sed -n 's/^constexpr \([A-Za-z_:0-9]*\) \([A-Za-z_][A-Za-z0-9_]*\) = \([A-Za-z_:0-9]*\);.*/\2 \1 \3/p' "$1"
}

# variants_probe <prefix> <file> <lo> <hi> <demo> - rewrite only the culprit
# region inside the *complete* file and compile the whole TU once per rewrite.
#
# "Which statement" is not actionable; "which property of the statement" is. Each
# row changes exactly one property and keeps everything else, including all the
# code before it, so a row that passes names a form of the same call that the
# writer can emit. The first row is the control: delete the statement and the rest
# of the TU has to compile, otherwise the flip was not this line's to begin with.
variants_probe() {
    local prefix="$1" full="$2" lo="$3" hi="$4" demo="$5"
    local region out="probe_src/var_${demo}.cpp" lit='"probe"' word name type init hit rlines

    matrix_row() {   # <label> <replacement region; empty deletes the region>
        local label="$1" body="$2" rc vd
        {
            head -n "$lo" "$full"
            [ -n "$body" ] && printf '%s\n' "$body"
            tail -n +"$((hi + 1))" "$full"
        } > "$out"
        replay_cl "$prefix" "probe_var.obj" "$out" "probe_var.log"; rc=$?
        vd="$(verdict_of "probe_var.log" "$rc" "probe_var.obj")"
        printf '  matrix row %-26s %-13s %s\n' "$label" "$vd" "$(objsize probe_var.obj)"
        [ "${vd:0:4}" = "pass" ] ||
            grep -oE 'error [A-Z][0-9]+' probe_var.log | head -1 | sed 's/^/     /'
    }

    region="$(sed -n "$((lo + 1)),${hi}p" "$full")"
    rlines="$(printf '%s\n' "$region" | wc -l | tr -d ' ')"
    echo "  ---- substitution matrix on the complete file (region L$((lo + 1))..L${hi}) ----"
    [ "$rlines" -eq 1 ] ||
        echo "     note: the region spans $rlines lines, so every row rewrites all of them"
    matrix_row "delete the statement" ""
    matrix_row "unchanged (control)" "$region"

    hit=0
    while read -r name type init; do
        [ -n "$name" ] || continue
        case "$region" in *"$name"*) ;; *) continue ;; esac
        hit=1
        matrix_row "imported const directly" "${region//$name/$init}"
        matrix_row "prvalue from the alias" "${region//$name/$type($name)}"
        matrix_row "local non-constexpr copy" "    $type probe_c = $name;${region//$name/probe_c}"
        matrix_row "local const ref to alias" "    const $type& probe_r = $name;${region//$name/probe_r}"
    done <<< "$(alias_decls "$full")"

    # The other operand: the line also hands over a char array that has to convert
    # to std::string_view. A literal removes that conversion and touches nothing
    # else, so it separates "the colour argument" from "this argument at all".
    for word in $(printf '%s\n' "$region" | tr -cs 'A-Za-z0-9_' '\n'); do
        grep -qE "^[[:space:]]*char ${word}\[" "$full" || continue
        matrix_row "string literal instead" "${region/$word/$lit}"
        hit=1
        break
    done
    # An axis that does not apply has to say so: a silent matrix is how a row that
    # was never run gets read as a row that passed.
    if [ "$hit" = 0 ]; then
        echo "     no rewrite applied: the region uses neither a constexpr alias nor a"
        echo "     char array, so neither the colour-declaration nor the string_view axis"
        echo "     can be reached from this statement"
    fi
    rm -f "$out" probe_var.obj probe_var.log
}

# bisect_prefix <demo> <prefix> <file> - the smallest prefix that still trips
# C1605, the culprit region verbatim, and the probes that only pay off once the
# region is known.
bisect_prefix() {
    local demo="$1" prefix="$2" full="$3"
    echo "================ bisect: ${demo}/Hud.cpp ================"
    if [ ! -f "$full" ]; then
        echo "  $full not found, skipping the bisect"
        return 0
    fi
    mkdir -p probe_src
    local -a L O
    L=(); O=()
    local l o
    while read -r l o; do
        [ -n "$l" ] || continue
        L+=("$l"); O+=("$o")
    done <<< "$(cut_points "$full")"
    local n=${#L[@]}
    if [ "$n" -lt 3 ]; then
        echo "  only $n usable cut points, nothing to bisect"
        return 0
    fi
    local lo=0 hi=$((n - 1)) mid=0 tries=0 deadline PV="" RC=0
    echo "  $n usable cut points, bisecting under a ${TIME_BUDGET}s budget"

    # Invariant: probe(lo) passes and probe(hi) fails. Both ends are established
    # before the range is split, because a file that fails from its first
    # statement or survives to its last one has no window to search.
    cut_probe "$prefix" "$full" "${L[lo]}" "${O[lo]}" "probe_bisect.obj"
    while [ "${PV:0:4}" != "pass" ] && [ "$lo" -lt "$((n - 2))" ]; do
        lo=$((lo + 1))
        cut_probe "$prefix" "$full" "${L[lo]}" "${O[lo]}" "probe_bisect.obj"
    done
    if [ "${PV:0:4}" != "pass" ]; then
        echo "  even the shortest prefix L${L[lo]} gave $PV - the trigger is in the preamble, not the body"
        return 0
    fi
    printf '  lo L%-4d %-6s %s\n' "${L[lo]}" "pass" "$(source_line "$full" "${L[lo]}")"
    cut_probe "$prefix" "$full" "${L[hi]}" "${O[hi]}" "probe_bisect.obj"
    if [ "$PV" != "C1605" ]; then
        echo "  the complete file gave $PV, not C1605: the probe does not reproduce the real build, bisect void"
        return 0
    fi
    printf '  hi L%-4d %-6s %s\n' "${L[hi]}" "C1605" "$(source_line "$full" "${L[hi]}")"

    deadline=$((SECONDS + TIME_BUDGET))
    while [ "$((hi - lo))" -gt 1 ]; do
        if [ "$SECONDS" -gt "$deadline" ]; then
            echo "  time budget reached, window stays [L${L[lo]}, L${L[hi]}]"
            break
        fi
        mid=$(( (lo + hi) / 2 )); tries=0
        cut_probe "$prefix" "$full" "${L[mid]}" "${O[mid]}" "probe_bisect.obj"
        while [ "${PV:0:5}" = "other" ] && [ "$tries" -lt 3 ] &&
              [ "$((mid - 1))" -gt "$lo" ]; do
            tries=$((tries + 1)); mid=$((mid - 1))
            cut_probe "$prefix" "$full" "${L[mid]}" "${O[mid]}" "probe_bisect.obj"
        done
        case "$PV" in
            C1605) hi=$mid ;;
            pass*) lo=$mid ;;
            *) echo "  inconclusive at L${L[mid]} ($PV), window stays [L${L[lo]}, L${L[hi]}]"; break ;;
        esac
        printf '  mid L%-4d %-14s window [%d, %d]  %s\n' "${L[mid]}" "$PV" \
            "${L[lo]}" "${L[hi]}" "$(source_line "$full" "${L[mid]}")"
    done

    echo "  minimal failing prefix: L${L[hi]} (last passing L${L[lo]})"
    echo "  ---- culprit region, verbatim (L$((L[lo] + 1))..L${L[hi]}) ----"
    sed -n "$((L[lo] + 1)),${L[hi]}p" "$full" | cut -c1-100 | sed 's/^/  | /'
    variants_probe "$prefix" "$full" "${L[lo]}" "${L[hi]}" "$demo"

    # Isolated: the culprit region dropped into the function with the rest of
    # the body deleted. Still failing means the region alone is the trigger;
    # passing means it only trips together with what came before it.
    local fstart iso isoopens isoV minimal v
    fstart="$(head -n "${L[lo]}" "$full" |
             grep -nE '^[A-Za-z].*::[A-Za-z0-9_~]+ *\(.*\{[[:space:]]*$' |
             tail -1 | cut -d: -f1)"
    if [ -n "$fstart" ]; then
        iso="probe_src/iso_${demo}.cpp"
        { head -n "$fstart" "$full"; sed -n "$((L[lo] + 1)),${L[hi]}p" "$full"; } > "$iso"
        isoopens="$(awk '{
            for (i = 1; i <= length($0); i++) {
                c = substr($0, i, 1)
                if (c == "{") b++; else if (c == "}") b--
            }
        } END { print b }' "$iso")"
        printf '%*s' "$isoopens" '' | tr ' ' '}' >> "$iso"
        replay_cl "$prefix" "probe_iso.obj" "$iso" "probe_iso.log"; RC=$?
        isoV="$(verdict_of "probe_iso.log" "$RC" "probe_iso.obj")"
        printf '  isolated region  %-14s (body from L%s, region L%d..L%d)\n' \
            "$isoV" "$fstart" "$((L[lo] + 1))" "${L[hi]}"
        case "$isoV" in
            pass*) ;;
            *) head -3 probe_iso.log | cut -c1-160 | sed 's/^/     /' ;;
        esac
        rm -f "$iso" probe_iso.obj probe_iso.log
    else
        echo "  isolated: no function header found before L${L[lo]}, skipped"
    fi

    # The optimiser itself, on the minimal repro: /Od is a different code path,
    # /Gw and /GS- change what gets described to the writer. None of the six
    # original variants touched this axis.
    minimal="probe_src/min_${demo}.cpp"
    cut_write "$full" "${L[hi]}" "${O[hi]}" "$minimal"
    for v in "/Od" "/Gw" "/GS-"; do
        run_variant "min $v" "$prefix $v" "probe_mv.obj" "$minimal"
    done
    listing_probe "$prefix" "$minimal"

    rm -f "$minimal" probe_cut.* probe_bisect.obj.* probe_mv.obj.* probe_iso.*
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

    bisect_prefix "$demo" "$prefix" "../src/demo/${demo}/Hud.cpp"
done

echo "probe complete (${SECONDS}s total)"
# Sweep, not a courtesy: this runs on a runner whose disk the build already
# used, and a killed /Fa writer can leave gigabytes behind if any variant
# reopened it. The probe's value is its log, not its intermediate files.
rm -rf probe_src probe_cut.cpp probe_listing_cmd.sh
rm -f probe_*.obj probe_*.obj.log probe_*.asm probe_*.log
exit 0
