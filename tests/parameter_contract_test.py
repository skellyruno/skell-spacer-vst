#!/usr/bin/env python3
"""Guard published automation IDs and plugin identity across engine migrations."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
processor = (root / "Source/PluginProcessor.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
ids = set(re.findall(r'juce::ParameterID\{"([^"]+)",\s*1\}', processor))
published = {"amount", "smooth", "link", "attack", "release", "lowcut",
             "highcut", "freeze", "power", "pan", "ms", "liveMode"}
missing = sorted(published - ids)
assert not missing, f"Published automation IDs removed: {missing}"
assert "PLUGIN_CODE Sk02" in cmake, "Plugin identity changed"
assert "PLUGIN_MANUFACTURER_CODE SkAu" in cmake, "Manufacturer identity changed"
assert 'BUNDLE_ID "audio.skell.spacer"' in cmake, "Bundle ID changed"
print(f"[PASS] {len(published)} published automation IDs and VST3 identity")
