#!/usr/bin/env python3
"""Static V16 host parameter identity regression."""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
processor = (root / "Source/PluginProcessor.cpp").read_text()
cmake = (root / "CMakeLists.txt").read_text()
ids = set(re.findall(r'juce::ParameterID\{"([^"]+)",\s*1\}', processor))
required = {"amount", "smooth", "attack", "release", "lowcut",
            "highcut", "schp", "sclp", "freeze", "power", "pan", "ms", "listen"}
missing = sorted(required - ids)
assert not missing, f"V16 automation IDs missing: {missing}"
assert "link" not in ids and "liveMode" not in ids, "Obsolete V7 controls remain exposed"
assert 'PLUGIN_CODE Sk02' in cmake, "Plugin identity changed"
assert 'PLUGIN_MANUFACTURER_CODE SkAu' in cmake, "Manufacturer identity changed"
assert 'BUNDLE_ID "audio.skell.spacer"' in cmake, "Bundle ID changed"
# Host UI uses -100..100 percent while the real-time engine expects -1..1.
assert re.search(r'p\.pan\s*=\s*pPan->load\(\)\s*\*\s*0\.01f\s*;', processor), (
    "Pan host-to-DSP conversion must divide by 100"
)
assert ids == required, f"unexpected changes to version-1 IDs: {ids ^ required}"
assert re.findall(r'juce::ParameterID\{"([^"]+)",\s*2\}', processor) == ["levelSensitivity"]
assert re.search(r'p\.levelSensitivity\s*=\s*pLevelSensitivity->load\(\)\s*\*\s*0\.01f\s*;', processor)
assert 'getChildWithProperty("id", "levelSensitivity")' in processor
assert re.findall(r'juce::ParameterID\{"([^"]+)",\s*3\}', processor) == ["smartSpace", "punchProtect", "bassProtect", "adaptiveRelease", "deltaListen"]
assert re.findall(r'juce::ParameterID\{"([^"]+)",\s*4\}', processor) == ["spectralFocus", "vocalFocus", "perceptualFocus", "harmonicProtect", "depthBudget"]
assert re.findall(r'juce::ParameterID\{"([^"]+)",\s*5\}', processor) == ["sonicTerritory", "humanEar", "componentProtect", "foresight"]
print(f"[PASS] V16 contract: {len(required)} legacy parameters + 1 version-2 + 5 version-3 + 5 version-4 + 4 version-5 parameters, VST3 identity")
