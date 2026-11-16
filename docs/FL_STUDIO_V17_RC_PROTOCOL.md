# 🩻 Skell Spacer V17 RC — FL Studio real-host verification

**Source baseline:** `main` at `6ce35c2`, V17 feature PR in `dev/spacer-v17-sound-host-release-20261115`.
**Release state:** candidate only. Linux DSP unit tests and Windows pluginval are not equivalent to FL Studio audio or UI performance.

## Install safely

Download the artifact from the **V17 PR run matching its exact source SHA**. Unzip the inner VST3 package. Verify `Skell Spacer.vst3/Contents/x86_64-win/Skell Spacer.vst3` is present and inspect the adjacent `build-info.json` for the source commit and SHA-256. Copy the folder to the standard VST3 path under `C:\\Program Files\\Common Files\\VST3`. Restart FL Studio and scan the VST3 plugins. Do not assume this package is an installer or signed public release.

## The five live checks

1. **Sidechain route.** Load test kit `REAL_VOCAL_KEY_15s.wav` (private producer-owned recording; do not upload to GitHub), `PAD_BEAT_15s.wav`, and `KICK_BASS_15s.wav` on separate mixer tracks. Put Skell Spacer on the pad mixer track. Route vocal mixer to it as a *sidechain only*, then enable/select the plugin's VST3 sidechain bus in FL Studio's wrapper. Start at Amount 50%, Attack 10 ms, Release 150 ms, Smooth 30%, Pan C. Verify the status changes to SIDECHAIN OK, and the pad drops in vocal-occupied frequencies.
2. **Listen bus-loss click test.** Engage Listen, then stop/bypass routing on the vocal mixer, and separately disable the plugin's sidechain bus through the host. Repeat at 64/128/256/512 samples. Confirm no click/cut, a smooth return to dry, and no crash. Record a short dry and wet render.
3. **Automation and project recall.** Automate Amount 0 → 100 → 0, Pan L100 → C → R100, Smooth, Attack/Release, filters, Freeze, Listen, and M/S. Save the FLP, close FL Studio, reopen the FLP, and verify exact state, GUI theme, and audible behavior.
4. **Performance.** Set 44.1/48/96/192 kHz (only rates supported by your driver) at 64/128/256/512 buffers. Compare Windows CPU graphs with UI open/closed, playback stopped, and during dense sidechain material. Record any underruns/glitches and note DAW and driver settings. A plugin reporting 0 added samples is not the same as zero audio-interface delay.
5. **Sound A/B and UI.** Compare dry/processed renders from the *same start point* at matched loudness. Check kick-to-bass and vocal-to-pad/bus use cases, spectral overshoots, pumping, stereo coherence, and sibilance. Check sizes and 100/125/150% Windows display scaling, all four actual themes, menus, knob typing, and the sidechain-filter toggler.

## What to return for release approval

FL Studio version; plugin SHA / build-info SHA; OS and CPU; driver and buffer;
pass/fail per case; a screenshot of each UI size; one FLP (only if you choose to share);
a 15-second dry/wet bounce; a note about any audio artifact or crash; peak CPU and underruns.
Do not label FL Studio verified until those checks are run in FL Studio.

## Test audio provenance

The private kit contains producer-owned, already provided **dry Deuce vocal** material extracted
from a saved offline listening page and locally generated instrumental signals. It is only
intended for local FL Studio review and must **never** be committed to the public repository.
The public CI A/B exports use explicitly synthetic source audio, not this recording.
