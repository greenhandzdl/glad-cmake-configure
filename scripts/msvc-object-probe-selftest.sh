#!/bin/bash
# msvc-object-probe-selftest.sh - exercise scripts/msvc-object-probe.sh without Windows.
# Usage: ./scripts/msvc-object-probe-selftest.sh
#
# Why this exists: the probe can only produce its data on a GitHub runner (C1605 is
# an MSVC object-writer limit, invisible to clang), one Windows build costs ~15
# minutes, and the first two rounds of the probe were each wasted on a bug in the
# probe itself rather than in the diagnosis - round one replayed ninja's native
# command line through Git Bash and got twelve `rc=127` lines, round two sampled
# every 4th cut point and returned a 12-line window instead of a statement. A CI
# round should be spent collecting evidence, not discovering that the tool does not
# run. Everything after the cl command line is plain shell, so it can be tested here.
#
# Method: a throwaway tree with a fake `cl.exe` on PATH that answers C1605 for any
# source longer than $SIM_FAIL_ABOVE lines (and, when asked for an /Fa listing on a
# failing source, streams a listing forever - which is the only way to test the
# watchdog short of a real runaway), and a copy of the probe whose
# `ninja -t commands` lookup is replaced by the equivalent command line. The real
# src/demo tree is used, so the cut points, the region dump and the isolated source
# are all derived from the actual failing files.
#
# It checks the shape of the output, not the diagnosis: the bisect must converge to
# two adjacent candidates, the culprit region must be printed, every probe row must
# carry a verdict (a row without one is how round one looked), the listing must have
# been killed and deleted, and the probe must exit 0. Expected line numbers are not
# hardcoded, because the candidate list moves when the HUD files are edited.
#
# The one number that is chosen on purpose is the threshold. Fake cl fails anything
# over 85 lines, which makes the minimal failing prefix L86 - the same single
# `hud.AddLine(buf, kDim);` MSVC reports (run 42777226104). That matters: with 86
# the fake flips one statement later, on a two-line snprintf that mentions no
# constexpr alias, and the colour-declaration axis then legitimately never runs - so
# the assertion that a reachable axis must not be skipped in silence would be testing
# nothing. Round 4's real bug was exactly that axis vanishing while another axis set
# the shared "something ran" flag.
#
# It also checks that nothing outlives the run. That is not paranoia: ten fake-cl
# writers from earlier rounds were still running hours later, each still appending
# to a listing whose directory had already been deleted, holding enough disk to fill
# the machine and leave `mktemp` failing with "no space left on device". A probe
# step that leaks writers is worse than one that prints nothing.

set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
probe="$here/msvc-object-probe.sh"
root="$(cd "$here/.." && pwd)"
[ -f "$probe" ] || { echo "no probe at $probe" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/gldx-probe-selftest.XXXXXX")" || exit 1
# Whatever happens, no fake compiler may keep running after this script: the
# leftover writers from earlier rounds each burned 20 minutes of CPU and tens of MB
# of "deleted" log per minute, and the machine ended up with no space for mktemp.
cleanup() {
    pkill -9 -f "$work/bin/cl.exe" 2>/dev/null
    rm -rf "$work"
}
trap cleanup EXIT
mkdir -p "$work/bin" "$work/build"
ln -s "$root/src" "$work/src" || exit 1

# --- fake cl.exe ---------------------------------------------------------------
cat >"$work/bin/cl.exe" <<'FAKE_CL'
#!/bin/bash
# Arg parsing follows what the probe actually emits: -c <src>, /Fo<obj>, /Fa<asm>.
src="" obj="" asm="" seen_c=0
for a in "$@"; do
    if [ "$seen_c" = 1 ]; then src="$a"; seen_c=0; continue; fi
    case "$a" in
        -c) seen_c=1 ;;
        /Fo*) obj="${a#/Fo}" ;;
        /Fa*) asm="${a#/Fa}" ;;
    esac
done
if [ ! -f "$src" ]; then
    echo "fake cl: no source given ($*)" >&2
    exit 2
fi
n="$(wc -l < "$src")"
if [ "$n" -gt "${SIM_FAIL_ABOVE:-85}" ]; then
    echo "$src: fatal error C1605: compiler limit: object file size cannot exceed 4 GB"
    if [ -n "$asm" ]; then
        while :; do
            for i in 1 2 3 4 5 6 7 8 9 10; do
                echo "  mov eax, DWORD PTR ?_Runaway@@3HA"
            done >> "$asm"
        done
    fi
    exit 1
fi
[ -n "$obj" ] && printf 'fakeobj' > "$obj"
if [ -n "$asm" ]; then
    for i in $(seq 1 500); do echo "  mov eax, DWORD PTR ?_Real@@3HA"; done > "$asm"
    echo "  ret" >> "$asm"
fi
exit 0
FAKE_CL

chmod +x "$work/bin/cl.exe"

# --- a copy of the probe whose ninja lookup becomes the equivalent command line --
# The command line the probe replays contains @, quotes and braces, so this is a
# literal substitution rather than sed/awk quoting.
python3 - "$probe" "$work/probe_under_test.sh" <<'PY'
import sys
src = open(sys.argv[1]).read()
old = '    cmd="$(ninja -t commands "$obj" 2>/dev/null | tail -1)"'
new = ('    cmd="$SIM_CL /O2 /Ob2 /DNDEBUG -std:c++latest -MD /bigobj /showIncludes '
       '@src/demo/modmap @x /Foout.obj /FS -c ../src/demo/${demo}/Hud.cpp"')
if old not in src:
    sys.exit("the ninja command lookup in the probe changed; update this selftest")
open(sys.argv[2], "w").write(src.replace(old, new))
PY
[ -f "$work/probe_under_test.sh" ] || exit 1

# --- run -----------------------------------------------------------------------
export PATH="$work/bin:$PATH"
export SIM_CL="$work/bin/cl.exe"
export SIM_FAIL_ABOVE="${SIM_FAIL_ABOVE:-85}"   # see the header: this lands the
                                                # fake culprit on the AddLine call
export PROBE_TIME_BUDGET="${PROBE_TIME_BUDGET:-120}"
export PROBE_ASM_BUDGET="${PROBE_ASM_BUDGET:-2}"

out="$work/out.log"
# The probe takes its build dir as a path relative to the cwd and reads the sources
# as ../src/..., so it has to run from the throwaway tree, not from the repo.
( cd "$work" && bash "$work/probe_under_test.sh" build ) >"$out" 2>&1
rc=$?

# --- checks --------------------------------------------------------------------
fails=0
check() {  # <description> <grep -E pattern>
    if grep -qE "$2" "$out"; then
        printf '  ok   %s\n' "$1"
    else
        printf '  FAIL %s  (missing: %s)\n' "$1" "$2"
        fails=$((fails + 1))
    fi
}

check_absent() {  # <description> <grep -E pattern>
    if grep -qE "$2" "$out"; then
        printf '  FAIL %s  (present: %s)\n' "$1" "$2"
        grep -E "$2" "$out" | head -3 | sed 's/^/     /'
        fails=$((fails + 1))
    else
        printf '  ok   %s\n' "$1"
    fi
}

echo "selftest: probe ran with rc=$rc against fake cl (threshold $SIM_FAIL_ABOVE lines)"
[ "$rc" -eq 0 ] || { echo "  FAIL probe must always exit 0"; fails=$((fails + 1)); }

check "the fake compiler was found and existed"     "compiler: .*\(exists: yes\)"
check "baseline reproduces C1605 (matrix is valid)" " +baseline +rc=1 +C1605"
check "skeleton compiles"                            " +skeleton +rc=0"
# Round one of the probe looked exactly like this: twelve rows, each with an exit
# code and not one compiler message. Any recurrence is a broken replay, not data.
check_absent "no variant died before reaching cl (the rc=127 failure mode)" "rc=127|rc=126|No such file or directory"
check_absent "no variant came back without a verdict" "no compiler error in the log"
check "bisect ran on glass_water"                    "bisect: glass_water/Hud.cpp"
check "bisect ran on night_forest"                   "bisect: night_forest/Hud.cpp"
check "bisect reports both ends"                     " +lo L[0-9]+ +pass"
check "bisect reports the failing end"               " +hi L[0-9]+ +C1605"
check "bisect converges to a minimal prefix"         "minimal failing prefix: L[0-9]+ \(last passing L[0-9]+\)"
check "culprit region is printed verbatim"           "culprit region, verbatim"
# The substitution matrix is the step that turns "which statement" into "which
# property of it". It has two families now, and they have to be separated: on the
# complete file a rewrite can only say "still broken", while the per-property rows
# are only readable on the minimal prefix. Both must run on both TUs.
check "matrix A ran on both TUs"          "matrix A: complete file"
check "matrix B ran on both TUs"          "matrix B: minimal failing prefix"
check "a matrix row reports a verdict"               "matrix row .*(pass|C1605|other)"
# The control of family B is the row that licenses every row after it: deleting the
# statement out of the minimal prefix has to compile, or the prefix is not minimal
# and the property rows mean nothing.
check "the prefix control row compiles"   "prefix: delete \(control\) +pass"
# And on *both* TUs: a control that only holds on one of them means family B is
# reading properties on one file and measuring noise on the other.
controls="$(grep -cE 'matrix row prefix: delete \(control\) +pass' "$out")"
if [ "$controls" = 2 ]; then
    printf '  ok   the prefix control compiles on both TUs\n'
else
    printf '  FAIL the prefix control passed on %s of 2 TUs - family B is void where it did not\n' "$controls"
    fails=$((fails + 1))
fi
check "redundant call sites are counted"  "complete: delete all [0-9]+ site"
# Which rows exist depends on what the culprit region happens to be, but a reachable
# axis must never be skipped in silence. Run 42777226104 is the reason this is a
# separate assertion instead of a disjunction: the alias regex did not tolerate the
# aligned `=`, so the colour-declaration axis produced no row *and* no message,
# because the char-array axis had already set the shared "some row ran" flag.
check "the alias axis is probed on these sources"    "prefix: imported const +(pass|C1605|other)"
check "the string_view axis is probed"    "prefix: string literal +(pass|C1605|other)"
# The other half of the same rule: the fake culprit for night_forest is a multi-line
# region that reaches neither axis, and that has to be *said* rather than left out.
# Silence and "not applicable" are the two outcomes this pair of assertions tells
# apart; without the second one the first could be satisfied by a single TU.
check "an unreachable axis announces itself"         "axis unreachable"
check "a multi-line region says so"                  "note: the region spans [0-9]+ lines"
check "isolated variant reports a verdict"           "isolated region +(pass|C1605|other)"
check "the optimiser axis is probed"                 "min /Od +rc="
# The fake cl never stops writing a listing for a failing source, so the only
# acceptable outcome here is rc=124: any other code means the watchdog let cl
# finish (or never ran), and the first version of this did exactly that - it
# killed the shell wrapper and left cl.exe appending to the listing afterwards.
check "the watchdog killed the runaway listing"      "listing +rc=124 +"
check "the listing showed the repeated emission"     "most frequent listing lines"
check "probe completes"                              "probe complete"

# The bisect must land on adjacent candidates: a window it cannot close has to be
# reported as a window, never as an answer.
if grep -qE "window stays" "$out"; then
    echo "  FAIL bisect stopped without converging:"
    grep -E "window stays|inconclusive" "$out" | sed 's/^/     /'
    fails=$((fails + 1))
fi

# Nothing may outlive the run. The probe's own kill_tree is what this checks, and
# the assertion is independent of the file check above: a writer that survives keeps
# appending to a path that has already been deleted, so it fills the disk while
# looking empty.
left_procs="$(pgrep -f "$work/bin/cl.exe" 2>/dev/null | tr '\n' ' ')"
if [ -z "$left_procs" ]; then
    echo "  ok   no fake cl process survived the probe (kill_tree reached the child)"
else
    echo "  FAIL fake cl still running after the probe: pids $left_procs"
    fails=$((fails + 1))
fi

# Disk hygiene: the watchdog test writes a runaway listing, so this also proves the
# sweep at the end of the probe actually removes it.
left="$(cd "$work/build" && ls -1 2>/dev/null | grep '^probe_' || true)"
if [ -z "$left" ]; then
    echo "  ok   no probe_* intermediate files left behind"
else
    echo "  FAIL probe_* files survived the sweep:"
    printf '%s\n' "$left" | sed 's/^/     /'
    fails=$((fails + 1))
fi

echo
if [ "$fails" -eq 0 ]; then
    echo "selftest passed: the probe's control flow works; only MSVC itself is still unproven."
    exit 0
fi
echo "selftest failed ($fails):"
sed 's/^/  | /' "$out"
exit 1
