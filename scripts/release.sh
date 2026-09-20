#!/usr/bin/env bash
# release.sh - Sync the project version across every place it is written.
#
# The version string lives in several files (source, build config, docs). When
# a release is cut by hand these drift apart (a git tag moves but the HUD
# version and the "current release" links stay behind). This script rewrites
# all known version anchors in one shot so a release is internally consistent.
#
# It NEVER pushes and NEVER creates the GitHub Release: those are deliberate,
# separate steps. See the summary it prints at the end.
#
# Usage:
#   scripts/release.sh <X.Y.Z>                 # only rewrite the version files
#   scripts/release.sh <X.Y.Z> --commit        # also stage + commit the bump
#   scripts/release.sh <X.Y.Z> --commit --tag  # also create git tag vX.Y.Z
#
# Anchors handled:
#   src/gfx/core/Platform.h   kAppVersion = "X.Y.Z"   (canonical source)
#   CMakeLists.txt            project(GLFW_Template VERSION X.Y.Z ...)
#   README.md                 sample "GLFW_Template X.Y.Z configuration:"
#   README.md                 "当前正式版 [vX.Y.Z](.../releases/tag/vX.Y.Z)"
#   AGENTS.md                 "当前正式版 tag：`vX.Y.Z`"

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
cd "$ROOT"

DO_COMMIT=false
DO_TAG=false
NEW=""

for arg in "$@"; do
    case "$arg" in
        --commit) DO_COMMIT=true ;;
        --tag)    DO_TAG=true ;;
        -h|--help) sed -n '2,26p' "$0"; exit 0 ;;
        --*) echo "unknown option: $arg" >&2; exit 2 ;;
        *)
            if [ -z "$NEW" ]; then NEW="$arg"
            else echo "expected a single version argument, got another: $arg" >&2; exit 2; fi
            ;;
    esac
done

if [ -z "$NEW" ]; then
    echo "usage: scripts/release.sh <X.Y.Z> [--commit] [--tag]" >&2
    exit 2
fi
if ! printf '%s' "$NEW" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
    echo "invalid version '$NEW': expected MAJOR.MINOR.PATCH (e.g. 1.2.0)" >&2
    exit 2
fi
if [ "$DO_TAG" = true ] && [ "$DO_COMMIT" != true ]; then
    echo "--tag requires --commit (the tag must point at the version bump commit)" >&2
    exit 2
fi

PLATFORM_H="src/gfx/core/Platform.h"
if [ ! -f "$PLATFORM_H" ]; then
    echo "not run from the project root (missing $PLATFORM_H)" >&2
    exit 1
fi
OLD="$(sed -nE 's/.*kAppVersion[[:space:]]*=[[:space:]]*"([0-9]+\.[0-9]+\.[0-9]+)".*/\1/p' "$PLATFORM_H")"
echo "current version (from $PLATFORM_H): ${OLD:-<unknown>}"
echo "target version: $NEW"
echo "-----------------------------------------"

# patch <file> <sed-script>
# Applies an extended-regex script, reports whether the file actually changed.
changed=0
patch() {
    local f="$1" script="$2" tmp
    [ -f "$f" ] || { echo "  MISSING: $f" >&2; return; }
    tmp="$(mktemp)"
    sed -E "$script" "$f" > "$tmp"
    if cmp -s "$f" "$tmp"; then
        echo "  unchanged: $f"
    else
        mv "$tmp" "$f"
        echo "  updated:   $f"
        changed=$((changed + 1))
    fi
    rm -f "$tmp" 2>/dev/null || true
}

V='[0-9]+\.[0-9]+\.[0-9]+'

patch "$PLATFORM_H" "s/(kAppVersion[[:space:]]*=[[:space:]]*\")$V(\".*)/\1$NEW\2/"
patch "CMakeLists.txt" "s/(project\(GLFW_Template VERSION )$V/\1$NEW/"
patch "README.md" "s/(GLFW_Template )$V( configuration:)/\1$NEW\2/"
patch "README.md" "s#(releases/tag/v)$V#\1$NEW#"
patch "README.md" "s/\[v$V\]/[v$NEW]/"
patch "AGENTS.md" "s/\`v$V\`/\`v$NEW\`/"

echo "-----------------------------------------"
echo "version anchors touched in $changed file(s)"

# Sanity: no stale old-version anchors should remain in the tracked files.
if [ "$OLD" != "$NEW" ] && [ -n "$OLD" ]; then
    if grep -RnE "$OLD" CMakeLists.txt "$PLATFORM_H" README.md AGENTS.md >/dev/null 2>&1; then
        echo "WARNING: the old version '$OLD' still appears in a version file:" >&2
        grep -RnE "$OLD" CMakeLists.txt "$PLATFORM_H" README.md AGENTS.md >&2 || true
    fi
fi

if [ "$DO_COMMIT" = true ]; then
    echo "-----------------------------------------"
    git add CMakeLists.txt "$PLATFORM_H" README.md AGENTS.md
    git commit -m "chore(release): v$NEW"
    echo "committed version bump"
    if [ "$DO_TAG" = true ]; then
        git tag -a "v$NEW" -m "v$NEW"
        echo "tagged v$NEW"
    fi
fi

echo "========================================="
echo "Next steps (done manually on purpose):"
echo "  1. push the branch and tag:"
echo "       git push origin master --follow-tags"
echo "  2. trigger the multi-platform release workflow with this tag:"
echo "       gh workflow run \"Build & Release\" -f build_type=Release -f release_tag=v$NEW"
echo "========================================="
