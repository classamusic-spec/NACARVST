#!/usr/bin/env bash
# ---------------------------------------------------------------------------
#  Compiles ONE factory preset file, with the real build's flags, and throws
#  the object away.
#
#  It exists because the preset library is written by many hands at once and a
#  full build is neither fast enough to iterate against nor safe to run
#  concurrently - two ninja invocations in one build directory will truncate
#  each other's output.  This touches nothing in build/.
#
#    Tools/scripts/check-preset-file.sh Source/Presets/Factory/Leads.cpp
#
#  A clean run prints nothing and exits 0.  It proves the file compiles under
#  -Werror-grade warnings: that every PID exists, every choice constant is
#  named, and no narrowing or shadowing crept in.  It proves nothing about
#  what the preset sounds like.
# ---------------------------------------------------------------------------
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <path-to-preset-cpp>" >&2
    exit 2
fi

SRC="$1"
[[ -f "$SRC" ]] || { echo "no such file: $SRC" >&2; exit 2; }

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
JUCE="${JUCE_DIR:-/home/user/JUCE}"
GEN="$ROOT/build/Tools/NacarBench_artefacts/JuceLibraryCode"

[[ -d "$GEN" ]] || { echo "configure the build once first: cmake --build build --target NacarBench" >&2; exit 2; }

OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

c++ \
    -DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 \
    -DJUCE_STRICT_REFCOUNTEDPOINTER=1 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 \
    -DJUCE_MODAL_LOOPS_PERMITTED=0 -DLINUX=1 -DNDEBUG=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_basics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_formats=1 \
    -DJUCE_MODULE_AVAILABLE_juce_audio_processors=1 \
    -DJUCE_MODULE_AVAILABLE_juce_core=1 \
    -DJUCE_MODULE_AVAILABLE_juce_data_structures=1 \
    -DJUCE_MODULE_AVAILABLE_juce_dsp=1 \
    -DJUCE_MODULE_AVAILABLE_juce_events=1 \
    -DJUCE_MODULE_AVAILABLE_juce_graphics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_gui_basics=1 \
    -DJUCE_MODULE_AVAILABLE_juce_gui_extra=1 \
    -I"$GEN" -I"$ROOT/Source" -I"$JUCE/modules" -I/usr/include/freetype2 -I/usr/include/libpng16 \
    -std=c++20 -O0 -fsyntax-only \
    -Wall -Wextra -Wpedantic -Wshadow -Wfloat-equal -Wsign-conversion \
    -Wmissing-field-initializers -Wzero-as-null-pointer-constant \
    -Wno-unused-parameter -Wno-unused-function \
    -Werror \
    "$SRC"
