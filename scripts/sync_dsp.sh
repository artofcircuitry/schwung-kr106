#!/usr/bin/env bash
# Sync the KR-106 DSP core (header-only, JUCE-free) from the main repo
# into src/dsp/kr106/. Run after pulling upstream DSP changes.
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
KR106_DIR="${KR106_DIR:-$REPO_ROOT/../ultramaster_kr106}"

if [ ! -f "$KR106_DIR/Source/DSP/KR106_DSP.h" ]; then
    echo "error: KR-106 repo not found at $KR106_DIR (set KR106_DIR)" >&2
    exit 1
fi

mkdir -p "$REPO_ROOT/src/dsp/kr106"
cp "$KR106_DIR"/Source/DSP/*.h "$REPO_ROOT/src/dsp/kr106/"
cp "$KR106_DIR/Source/KR106_Presets_JUCE.h" "$REPO_ROOT/src/dsp/kr106/"
echo "Synced DSP headers from $KR106_DIR"
