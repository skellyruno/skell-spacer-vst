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


## 2026-11-19 — Private Windows VST3 installer

A separate release branch based on PR #9's exact passing head adds `Install-SkellSpacerPreview.ps1`, a per-user installer, update/backup and rollback flow, package-integrity checks, and a PowerShell self-test. The package remains an unsigned `PREVIEW_ONLY` VST3; it does not set commercial release eligibility.

The Windows job stages the installer and two notices, writes a provisional source/build/binary/moduleinfo manifest, runs the install/update/rollback self-test and a real-package `-WhatIf` no-write check, then marks installer and rollback gates passed only if those checks pass. The final checksum manifest covers the binary, moduleinfo, build-info, installer and both documentation files.

Still required independently: FL Studio scan and sidechain routing, state save/reopen, real audio A/B and owner-hardware CPU/latency, local Quick4 renderer, license/attribution review, owner-approved vendor URL/support email, signing, storefront and pricing/trial approval. Keep this PR draft; no public release is authorized.


Security follow-up: the installer now checks every existing path component from the volume root to the package/install/backup destinations and rejects junctions, symlinks and other reparse points. A Windows junction regression assertion covers a redirected parent path. Users with redirected `LOCALAPPDATA` must supply non-reparse custom install and backup paths.
