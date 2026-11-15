# V16 source-import validation — November 15, 2026

Uploaded reference: `skell-spacer-vst-v16.zip`.
SHA-256: `2150c24a2cf60620ddd28ce15cf16d708d6fe93fa67c46cdfc42b4b5935cc2f8`.

## Local checks performed against V16 source

- ZIP CRC integrity: passed, 61 entries.
- G++ 17 `tests/live_dsp_test.cpp` at -O2 -Wall -Wextra -Wpedantic -Wshadow: passed, zero failures.
- G++ 17 `tests/stress_test.cpp` at the same flags: passed, zero failures.
- Compile-only -Wconversion -Werror on both DSP tests: passed.
- Static parameter contract guard on patched V16 tree: passed.
- Full CMake configure/VST3 build: blocked, no cached JUCE and no outbound DNS to fetch JUCE.
- Quick4 live-editor screenshots and FL Studio host test: not performed.

## Migration risk

The uploaded V16 removes the former `link` and `liveMode` parameters and removes the historical Studio FFT audio processing mode. Replacing the code without a migration would break previously published automation IDs and change the sound of old saved sessions. The patched V16 candidate locally retains both IDs for state decoding, but old DSP semantics are **not** restored; old session A/B remains required.

## PR scope

This PR adds only a compatibility gate and PR-only CI cancellation. **It does not import V16 DSP/UI source files.** The full source import is still pending. Do not mark the V16 update or Windows VST3 artifact as complete on the strength of this gate alone.
