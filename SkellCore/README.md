# SkellCore v0.3 — shared SKELL VST foundation

Portable C++17 DSP and optional JUCE 8 UI targets. This is an **initial tested foundation**, not a migration of every existing production-grade engine.

| API | Status | Scope |
| --- | --- | --- |
| `TempoClock` | Portable tests pass | Host BPM/PPQ clock, beat and sample math |
| `PhraseTracker`, `OnsetDetector` | Portable tests pass | Lightweight envelope-based segment/onset cues; not language-aware |
| `FDNReverb` | Portable tests pass | Four-delay Hadamard feedback network (new algorithm; no claim it matches existing reverb) |
| `TruePeakLimiter` | Portable regression/parity tests pass | Standard 4x/16-tap or opt-in Economy 2x/8-tap interpolation. **Neither mode is true-peak certified.** |
| `PitchFormantEngine` | Imported SkellyTune backend available | Adapter keeps original engine available without silently changing existing plugins; dry pass-through until attached |
| `ui::Theme`, `ui::LookAndFeel`, `ui::GlowCache` | Windows JUCE VST3 build verified | Reusable skin and bounded glow cache. Existing plugin UIs unchanged |

**Do not switch existing SkellyTune, DeEsser, Master, or Choir DSP paths to these new modules without A/B testing**. SkellCore is organized for gradual extraction of validated code and stable version pinning.

## Use as a subdirectory

```cmake
add_subdirectory(SkellCore)
target_link_libraries(MyPlugin PRIVATE SkellCore::DSP)
# After JUCE is set up:
target_link_libraries(MyPlugin PRIVATE SkellCore::UI)
```

```cpp
#include <SkellCore/Core.h>
// Only in PluginEditor / UI sources:
#include <SkellCore/UI.h>
```

## Use from a different plugin repo

Pin the SkellCore source to an immutable commit rather than `main` or a moving branch:

```cmake
include(FetchContent)
FetchContent_Declare(SkellCore
  GIT_REPOSITORY https://github.com/skellyruno/skell-vst-template.git
  GIT_TAG <COMMIT_WITH_SKELLCORE>
  SOURCE_SUBDIR SkellCore)
FetchContent_MakeAvailable(SkellCore)
target_link_libraries(MyPlugin PRIVATE SkellCore::DSP)
```

When SkellCore gets its own repository, update only `GIT_REPOSITORY` and keep the pinned SHA/release approach. Do not copy each library source into each plugin.

## Tests

```sh
cmake -S . -B build -DSKELLCORE_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

The portable tests do not need JUCE, a DAW, or any audio hardware. Remaining release gates: multi-rate audio fixtures, oversampled peak reference vectors, PDC checks, CPU benchmarks, sanitizers and host tests on FL Studio/Reaper.

## Migration sequence

1. Build and pin the shared library; do not change plugin IDs or parameter IDs.
2. Connect `SkellCore::DSP` in the template and choir without changing active processing.
3. Refactor DeEsser's existing `PhraseAwareDetector` behind an adapter (preserve event semantics and thresholds).
4. Implement and A/B test a SkellyTune adapter maintaining original output-delay contracts and formant controls.
5. Compare Master limiter and SkellCore limiter via true-peak reference signals. Move only if equivalent or intentionally better.
6. Extract any stable common UI widgets one at a time; maintain skin-specific themes and assets.
7. Use versioned, pinned dependencies in AdlibLab, Silence Designer, Choir and the new plugins when their repos exist.

Every significant DSP change must pass golden WAV comparisons, latency and denormal tests before becoming the default.


## v0.2 production source integration
The library now contains **real upstream DSP code**, not merely empty interface stubs:

- `backends/tune/`: copied SkellyTune RealtimeTuner v3.3 and its seven supporting headers, under SkellyTune's MIT license.
- `backends/SkellyTuneBackend.h`: directly forwards planar buffers and preserves the original fixed dry/transient delay declaration. This is not a new pitch algorithm.
- `backends/deesser/PhraseAwareDetector.h`: namespaced copy of DeEsser's live phrase-aware sibilance detector. Its algorithm, window lengths and sensitivity mapping are preserved.
- `backends/master/MasteringEngine.h`: namespaced copy of Skelly Master's oversampled-frame DSP. Its external oversampling and latency requirements remain intact.
- `HostTempoClock.h`: optional JUCE playhead position adapter.
- `tests/test_integrations.cpp`: portable backend smoke tests (must run in CI before migration).

These are **versioned reference copies** and must not be silently rolled forward as upstream repos change; changes require source SHA updates and A/B audio fixtures. The new generic FDN and limiter are independent prototypes and are not substitutes for the production engines without validation.

## v0.3 low-CPU and safety testing

See [PERFORMANCE.md](PERFORMANCE.md) for Economy versus Standard filter cost, Linux/Windows real-time safety tests, ASan/UBSan, zero-allocation checks, reference-output parity and GitHub runner benchmarks. These measurements are not a guarantee on an older physical PC.
