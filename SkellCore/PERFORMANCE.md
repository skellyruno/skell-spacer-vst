# SkellCore low-CPU / safety profile (v0.3 candidate)

SkellCore DSP is for real-time audio. No heap allocation, file reads, mutexes or image rendering should occur in processFrame/processBlock. Always call prepare while the audio thread is suspended.

## Two limiter profiles

| Profile | Interpolation detector work per stereo sample | Purpose |
| --- | ---: | --- |
| Economy | 2 phases x 8 taps x 2 channels = 32 multiply-accumulate iterations | Lower-cost preview and many-instance chains |
| Standard (default) | 4 phases x 16 taps x 2 channels = 128 iterations | Preserve existing Standard limiter output |

Economy reduces detector arithmetic, **not** total plugin CPU by exactly 75%. Neither profile is certified BS.1770 true-peak compliant. For final mastering, the limiter requires further verification with intersample peak reference vectors. The sampling latency remains 3 ms in either profile. Always report limiter.latencySamples() to the host during preparation.

Choose the profile in prepareToPlay while stopped (do not change quality within audio callback):

    skellcore::TruePeakLimiter limiter;
    limiter.prepare(sampleRate, 2,
        skellcore::TruePeakLimiter::Quality::Economy);
    const auto hostLatency = limiter.latencySamples();

## Core safety fixes

- Before prepare, FDNReverb and TruePeakLimiter return sanitized dry frames instead of dereferencing uninitialized buffers.
- Sample rates are sanitized and capped at 192 kHz in portable foundation components to reject corrupt values and unbounded allocations.
- Reverb caches gain multipliers outside the per-sample hot path and flushes extremely small damping state to zero.
- Limiter uses a mirrored history ring, removing modulo operations from each FIR tap.
- UI GlowCache rejects abnormal image sizes (>1,572,864 pixels or >2,048 on an axis) and reuses identical cached images, preventing huge memory allocations during resize.
- Existing Release-mode tests have NDEBUG explicitly undefined so assert() actually tests behavior in CI.
- A low-CPU safety regression checks no new/delete calls from reverb/limiter processing, multiple rates, NaN/Inf input and unprepared processing.
- A separate reference implementation compares the new Standard limiter with the previous output at 44.1, 48, 96 and 192 kHz.

## CPU measurements

Benchmark is an informational wall-clock time measurement of 4 s of stereo 48-kHz audio, not a CI pass/fail threshold. Hosted runner timings are **not** proof of performance on old PCs.

    cmake -S SkellCore -B build-core -DCMAKE_BUILD_TYPE=Release -DSKELLCORE_BUILD_TESTS=ON -DSKELLCORE_BUILD_BENCHMARKS=ON
    cmake --build build-core --config Release --parallel 2
    ctest --test-dir build-core --build-config Release --output-on-failure
    ctest --test-dir build-core --build-config Release -R SkellCoreBenchmark -V

Recommended real target: 5, 10 and 20 simultaneous instances in FL Studio on low-end Windows hardware, with 64/128/256 sample buffers at 44.1/48 kHz. Profile worst-case loud vocal material, not silence, and measure overruns/dropouts, CPU per callback, RAM and plugin latency. Keep all production DSP modules on their existing defaults until A/B and CPU verification.

## Still outside this release

SkellyTune's imported realtime pitch engine, DeEsser phrase-aware backend and MasteringEngine are not altered by these changes. Their multi-instance CPU use, latency stability and sound quality require separate host-level profiling and golden WAV comparisons. No real FL Studio host validation has been performed in this commit.