# SkellCore VST Template
Shared **Skell Audio** foundation for JUCE 8 C++17 VST3 plugins.

The former Skell VST Quickstart is preserved as a **small gain demo plugin**, with its original VST3 manufacturer, plugin code and bundle identity unchanged. Build a new plugin by choosing unique IDs; never reuse these demo IDs in commercial releases.

## Contents
- `SkellCore::DSP`: portable C++17 TempoClock, PhraseTracker, OnsetDetector, four-line FDNReverb, prototype 4× interpolated-peak limiter, PitchFormantEngine.
- `SkellCore::UI`: JUCE LookAndFeel, colour themes and glow cache.
- **Production DSP backend copies**: SkellyTune RealtimeTuner v3.3 and dependencies (MIT), exact DeEsser PhraseAwareDetector behavior in a namespaced copy, and Skelly Master MasteringEngine in a namespaced copy.
- `SkellCore/backends/SkellyTuneBackend.h`: adapter matching production pitch backend to the shared `PitchFormantEngine`.
- Separate portable CI (Windows + Linux) and Windows JUCE VST3 build pipeline.

## Generate a new plugin (recommended)

```sh
python tools/new_plugin.py "Skell Compressor" ../skell-compressor-vst --code SC01
```

This creates an independent VST3 + Standalone project with an immutable SkellCore dependency, unique plugin code, generated CI, and the original quickstart gain processor. Pick a different four-character code for **every** plugin. Once you rename the GitHub template repository, use `--core-repo https://github.com/skellyruno/skellcore-vst-template.git`.

## Low-end PC performance and safety

SkellCore offers an opt-in `TruePeakLimiter::Quality::Economy` detector (2 phases × 8 taps, versus default Standard's 4 × 16), modulo-free FIR reads, bounded buffer allocations, pre-prepare bypass safety, a bounded UI glow cache, and actual Release-mode allocation/parity tests. Read [performance details](SkellCore/PERFORMANCE.md) before enabling Economy in any shipped plugin.

## Build
```sh
cmake -S . -B build -DJUCE_PATH=/path/to/JUCE
cmake --build build --config Release
```

`JUCE_PATH` is optional; CMake otherwise fetches pinned JUCE 8.0.12.
```sh
cmake -S SkellCore -B build-core -DSKELLCORE_BUILD_TESTS=ON
cmake --build build-core --config Release
ctest --test-dir build-core -C Release --output-on-failure
```

## Reuse in other plugin repos
Copy `SkellCore/` into your monorepo, or FetchContent the **immutable commit** of this template. Example from a consumer:
```cmake
include(FetchContent)
FetchContent_Declare(SkellCore
  GIT_REPOSITORY https://github.com/skellyruno/skell-vst-template.git
  GIT_TAG <IMMUTABLE_COMMIT_SHA>
  SOURCE_SUBDIR SkellCore)
FetchContent_MakeAvailable(SkellCore)
target_link_libraries(MyPlugin PRIVATE SkellCore::DSP SkellCore::UI)
```
Replace `MyPlugin` with the JUCE target. Once the repository itself is renamed on GitHub, update the URL to `skellcore-vst-template` while pinning the immutable revision. Repositories renamed by GitHub redirect old clones but new URLs should be adopted in each plugin.

## Integration APIs
```cpp
#include <SkellCore/Core.h>
#include <SkellCore/backends/SkellyTuneBackend.h>
skellcore::backends::SkellyTuneBackend tuning;
skellcore::PitchFormantEngine pitch;
pitch.setBackend(&tuning);
pitch.prepare(48000, 2);
auto latency = pitch.latencySamples(); // Report from prepareToPlay.
```
The production tuner exposes block processing and stable declared dry/transient latency. Do not change PDC inside processBlock and do not confuse variable pitch-cycle source age with fixed host delay.

Include `<SkellCore/backends/deesser/PhraseAwareDetector.h>` and use `skellcore::deesser::PhraseAwareDetector` for the existing DeEsser logic, and `<SkellCore/backends/master/MasteringEngine.h>` for the sampled-at-oversampled-rate mastering core. The mastering engine needs a high-quality *external* oversampler. The generic `TruePeakLimiter` is experimental; no compliance claim.

## Status and migration
This is **v0.3 shared-core work**. Source import does not automatically migrate any already released plugin. Existing DAW automation, UI layout, plugin IDs, and audio behavior must stay unchanged until validated per-plugin.

- Tuning, phrase aware, and mastering algorithms: imported, require A/B tests before plugin adoption.
- FDN reverb, prototype true peak limiter, onset tracking: functional portable modules, not production-certified.
- Host tempo / JUCE UI: available, UI not mandatory for a plugin.
- Choir: links pinned SkellCore target on separate PR; current sound unchanged.
- AdlibLab / Silence Designer / other new plugins: opt in as their CMake projects are ready.

See [SkellCore/README.md](SkellCore/README.md) and [GitHub migration tracker](https://github.com/skellyruno/skell-vst-template/issues/2).
