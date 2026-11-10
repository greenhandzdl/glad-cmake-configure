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

set -uo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
probe="$here/msvc-object-probe.sh"
root="$(cd "$here/.." && pwd)"
[ -f "$probe" ] || { echo "no probe at $probe" >&2; exit 1; }

work="$(mktemp -d "${TMPDIR:-/tmp}/gldx-probe-selftest.XXXXXX")" || exit 1
trap 'rm -rf "$work"' EXIT
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
if [ "$n" -gt "${SIM_FAIL_ABOVE:-86}" ]; then
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
export SIM_FAIL_ABOVE="${SIM_FAIL_ABOVE:-86}"
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
