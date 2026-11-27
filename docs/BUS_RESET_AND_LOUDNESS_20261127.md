# Bus reset, cached controls and listening alignment

Parent 5b19b80 portable and Windows workflow DSP job passed; Windows build was still in progress at start of this milestone. No changes to UI, parameter IDs or default processing.

## DSP
- Fixed stale main right-hand bell state and filtered right key history on channel-count changes. Old 44.1k reconnect fixture replayed 0.18167825 key / 0.152292758 main peak-amplitude samples into silent reconnected channels; corrected engine outputs zero for both. All four sample rates passed, reset matches independent fresh LR stream exactly after M/S.
- Cache sample-rate-dependent low/vocal/perceptual coefficients; reuse manual brush weight once per tick and skip redundant unchanged bell coefficient updates. Four-instance strict parent benchmark 44.1/48/96/192k had exact sample and overlap/protection parity. Single run timing changes -0.08/-0.60/+7.78/+5.13%; mixed small results, no across-the-board CPU improvement claim.
- Two reused private recorded-vocal controlled mixes, Classic and combined Smart: exact sample identity against prior renderer in all four renders. This is compatibility validation, not better perceptual clarity.
- Address/undefined behavior sanitizer bus-reset tests, four-rate allocation check (0 detected C++ new calls, 0 reported latency), native cached JUCE 8.0.12 build, 12 actual native screenshot scenes and 121 state checks passed locally. Existing layout and controls preserved.

## Listening and reference workflow
- Use offline pyloudnorm DeMan K-weighting and gated integrated LUFS matching; one common attenuation for headroom, no limiter. Four-times polyphase peak estimate target -1 dB, verify actual matched loudness and reject silence, invalid alignment or >12 LU relative gain correction. Python dependency required only for offline research, not the VST3.
- Local Python regression passed including old offset/bypass rejection, held-out fallback, mono-safe steering and new K-weighting/peak/silence assertions. CI checks syntax of Python research tools; no dependency downloads introduced.
- Six listening files in two private cases independently verified with FFmpeg EBU R128: all three A variants -13.2 LUFS, all three B variants -13.5 LUFS; rounded true peaks <=-1.0 dBTP. Source audio stays private. Listening preferences not yet submitted.
- Original Trackspacer wet render and physical FL Studio testing remain unavailable. Source separation, live Invisible Unmasking and live Auto Mix remain research.

No merge to main.
