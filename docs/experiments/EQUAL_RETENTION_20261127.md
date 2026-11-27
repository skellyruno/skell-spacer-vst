# Offline transient experiment — NOT a release candidate

This branch preserves research without superseding the in-progress e16d6ae Windows run. Do not apply the patch to production, merge, or ship it as a plugin. The production Source tree is unchanged. Reproduce by compiling tools/render_research.cpp from e16d6ae, then applying the patch in a separate scratch copy and compiling the second renderer. Run tools/equal_retention_benchmark.py with both renderer paths and explicitly aligned owner-provided 48 kHz stereo bed/key WAVs. No private audio or source filenames included.

Three new independent recorded-music excerpts combined with three separate vocal takes (not confirmed isolated stems). One development recording; two locked holdout cases. Eleven candidate variations tested on development only. Selected m3j with derivative fast/slow transient recovery and directional masking salience; compiled as a portable offline renderer. Amount calibration matches bed energy without selecting using holdout quality. At baseline Amount 35%, matched Amount 44.9668%, 42.875%, 42.7520%; retention mismatch <=0.006 percentage points. Independent ERB-weighted STFT and dry-defined flux measures, not perception or drum-ground-truth tests.

| Case | Bed retention old/new % | Auditory masking reduction old/new % | Transient-associated power old/new % |
|---|---|---|---|
| Development | 97.0342 / 97.0334 | 10.4237 / 10.6330 | 97.4811 / 97.5451 |
| Holdout 1 | 99.9208 / 99.9199 | 0.9439 / 0.9819 | 99.9479 / 99.9516 |
| Holdout 2 | 55.0586 / 55.0640 | 17.7882 / 17.5835 | 45.4958 / 48.8992 |

Decision: do not promote. Second holdout fails masking nonregression; candidate is a broadband transient recovery blend on top of local protection and has no complete live automation/state/CPU/host validation. No claim of true component separation. Private blinded LUFS-matched listening lab was tested in Chromium for synchronized switching, pause/seek, reset and export. FFmpeg EBU R128 verified rounded -18.2/-15.1/-14.0 LUFS for the three groups and all peaks below -1 dBTP. User feedback not yet submitted. Original vendor render unavailable.

e16d6ae exact source archive checksum and tree verified; complete historical Classic parity and portable CI passed, Windows build still running when this research was recorded. Pinned JUCE 8.0.12 reused; 14 real native Quick4 scenes passed, visually inspected; full stress passed 137 checks including 40 editor lifecycles, 2000 button clicks, 12105 audio blocks, six sample rates x five block sizes. No production UI or DSP change in this research branch.
