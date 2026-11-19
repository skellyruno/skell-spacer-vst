# V17 QA and release gate

- Latest verified baseline: V16 `main` commit `6ce35c2`, green Windows CI.
- V17 branch fixes abrupt Listen when host disconnects sidechain bus and adds regression.
- Portable 15-second wet/dry/key WAVs from the actual JUCE-free V16 algorithm are generated in Linux CI (not FL Studio).
- `tests/performance_matrix.cpp` captures callback p50/p99/deadline misses at 44.1/48/96/192 kHz, 64/128/256/512 frames, GUI getDisplay open and closed. Runner-specific timings are diagnostics, not universal CPU promises.
- CI version metadata verifies Windows PE32+ x64 binary and includes SHA-256 and source commit in the ZIP's `build-info.json`.
- UI audit on this V17 PR includes real JUCE 100/125/150% render scaling and existing layout/control regression tests.
- A private FL Studio test kit, using producer's previously supplied dry vocal, is created only in the ChatGPT workspace for optional real DAW testing. Never add user audio to GitHub.
- Pending: actual FL Studio VST3 scan, sidechain routing, on-host CPU and audio A/B, QA approval. Do not merge the draft PR or create a public release without verification.


## 2026-11-19 — Exact VST3 class inventory

The Windows package verifier now requires exactly two VST3 class records: one Audio Module Class and one Component Controller Class. Each class CID must be exactly 32 hexadecimal characters. This rejects unexpected third classes and malformed IDs before pluginval and package upload; the existing product version, class IDs, DSP, presets, parameter IDs, UI, and saved-state contract remain unchanged.
