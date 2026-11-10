#!/bin/bash
# Offscreen editor render + control checks. Needs a JUCE checkout and prebuilt module objects
# (besides each module .cpp: juce_graphics_Harfbuzz.cpp, juce_graphics_Sheenbidi.c, juce_core_CompilationTime.cpp, juce_audio_processors_headless.cpp); writes PNGs to the output dir.
#   tests/ui_render.sh <JUCE dir> <objects dir> <output dir>
set -e
JUCE=$1; OBJ=$2; OUT=$3; HERE=$(cd "$(dirname "$0")/.." && pwd)
INC="$OBJ/inc"
D="-DJUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1 -DJUCE_STANDALONE_APPLICATION=1 -DJUCE_WEB_BROWSER=0 -DJUCE_USE_CURL=0 -DJUCE_USE_XRANDR=0 -DJUCE_USE_XINERAMA=0 -DJUCE_USE_XCURSOR=0 -DJUCE_USE_XSHM=0 -DJUCE_USE_XRENDER=0 -DJUCE_MODAL_LOOPS_PERMITTED=1 -DNDEBUG=1"
for m in juce_core juce_events juce_data_structures juce_graphics juce_gui_basics juce_audio_basics juce_audio_processors juce_audio_processors_headless; do D="$D -DJUCE_MODULE_AVAILABLE_$m=1"; done
g++ -std=c++17 -O1 -Wall -Wextra -Wno-unused-parameter $D -I"$INC" -I"$JUCE/modules" -I"$HERE/Source" \
  "$HERE/tests/ui_render.cpp" "$HERE/Source/PluginProcessor.cpp" "$HERE/Source/PluginEditor.cpp" \
  "$OBJ"/juce_*.o -lX11 -lfreetype -lfontconfig -ldl -lpthread -lrt -o "$OUT/ui_render"
"$OUT/ui_render" "$OUT"
