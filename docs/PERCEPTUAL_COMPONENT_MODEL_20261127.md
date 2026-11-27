# Perceptual masking and musical component protection

## Model and compatibility

Fresh processor instances select perception model 2. Existing saved projects without `perceptionModelVersion` restore model 1; saving writes the model version so their original Human Ear and Component Protect settings retain their existing law. No new automatable parameters, selector entries, knobs or geometry. A fresh instance is required to use the refined law when migrating an old project. The low-level portable `SpacerParams` defaults to model 1 for compatibility. Both paths preserve the unchanged Classic default. Model selection only affects the existing Human Ear and Component Protect strengths. 

Human Ear model 2 caches directional ERB-derived excitation neighborhoods at prepare, distinguishing lower-to-higher from higher-to-lower masking. The cut-need proxy considers nearby instrumental masker power and nearby key power as well as the current band; it is bounded by the existing cut law. It is not calibrated to listening SPL and does not claim validated perceptual masking accuracy.

Component Protect model 2 uses per-band 80 ms causal power history and local onset rise alongside the existing broadband onset and occupancy. Bass prominence relative to adjacent bands informs tonal protection. Strong conflict limits protection. A hit in one band should not release unrelated sustained-band attenuation. No actual stem separation or lookahead. 

## Tests and release gate

Previous 04f33c9 failed historical Classic parity on layout transitions. Scope right-channel filter cleanup to active Smart to retain original Classic response; the Classic reconnect behavior is intentionally unchanged. Local four-rate Classic parity against the previously verified 017801f archive: zero audio and display error. Exact historical CI baseline remains required. Smart model-1 exact parity against 04f33c9 passed at four rates, as did Smart reconnect/reset. Model-2 disabled-control parity exact. Native state regression verifies old/fresh version persistence; full UI capture adds the existing Human Ear and Component Protect selectors. 

Controlled 1.4 kHz sustained tone plus 4.2 kHz transient, 48 kHz: unrelated sustained-band diagnostic protection 0.054317 -> 0.000003, transient-band peak 0.295504 -> 0.231644. All four sample rates passed the localized-protection regression and sanitizer runs. The hit retains positive protection but is less strongly protected; this is a deliberate tradeoff, not a drum-preservation measurement. Four-rate allocation stress alternates model flag; zero detected C++ new calls and zero reported latency. No CPU improvement claim.

Two reused private recorded-vocal test mixes, fixed amount 35%, Smart 100%, Clean Key 50%, Phase Protect 50%, Stereo Detail 100%, Human Ear and Component Protect 70%: direct spectral-overlap reduction A 9.8972 -> 10.1896%, B 8.9238 -> 9.0420%; bed energy retained A 98.5071 -> 98.4631%, B 98.8719 -> 98.8635%. Small extra attenuation; a spectral-overlap metric, not a perceptual listening result. No separate musical stem ground truth. Same inputs as prior listening research, not independent held-out songs. Offline research renderer accepts optional Human Ear, Component Protect and model flag. Private mixes use gated LUFS matching with peak headroom, no source audio in git. 

Original Trackspacer wet render, FL Studio and broader perceptual/stem-ground-truth validation remain outstanding. No merge to main.
