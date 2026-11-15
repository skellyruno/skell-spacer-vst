#!/usr/bin/env python3
"""Static guard against accidentally dropping published plugin IDs."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
processor = (root / "Source/PluginProcessor.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
ids = set(re.findall(r'juce::ParameterID\{"([^"]+)",\s*1\}', processor))
legacy = {"amount", "smooth", "link", "attack", "release", "lowcut",
          "highcut", "freeze", "power", "pan", "ms", "liveMode"}
missing = sorted(legacy - ids)
assert not missing, f"Existing automation IDs removed: {missing}"
assert 'PLUGIN_CODE Sk02' in cmake, "Plugin identity changed"
assert 'PLUGIN_MANUFACTURER_CODE SkAu' in cmake, "Manufacturer identity changed"
assert 'BUNDLE_ID "audio.skell.spacer"' in cmake, "Bundle ID changed"
# Host UI uses -100..100 percent while the real-time engine expects -1..1.
assert re.search(r'p\.pan\s*=\s*pPan->load\(\)\s*\*\s*0\.01f\s*;', processor), (
    "Pan host-to-DSP conversion must divide by 100"
)
print(f"[PASS] preserved {len(legacy)} legacy parameter IDs and VST3 identity")
