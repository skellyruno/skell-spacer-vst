# Smart Space milestone 2 — spectral decisions
Baseline ae1577e portable DSP, sanitizer and delta identity passed. No change to Classic defaults or existing IDs. Five additional host percentages (version 4, default 0) are opt-in inside Smart Space, 50 ms strength transitions.
- Spectral Focus reduces spread-only cuts using raw vs spread key power (adaptive target width, not variable filter-bank resolution).
- Vocal Focus moderates high-dominant key spectra; this is a sibilance/noise proxy, not phoneme classification.
- Perceptual Focus weights the speech-presence region more heavily than extremes (heuristic, not a validated perceptual model).
- Harmonic Protect preserves prominent main-signal partials when overlap is weak (local prominence heuristic, not pitch tracking).
- Adaptive Depth reduces requests when key power dominates the track in a band; no added boost.
- Diagnostics estimate residual modeled conflict using realized cut coefficients and band-center kernels at display cadence. Relative model, not a perceptual score or a real audio separation metric.
- Verify four-rate active Smart allocation/automation, bounded control laws, delta identity, modeled conflict, synthetic two-tone energy and native state migration/recall. No main merge. Native UI and real reference listening still required.
