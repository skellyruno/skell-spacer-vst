# skell-spacer-vst

A Trackspacer-style **sidechain spectral ducker** (VST3, JUCE 8). Put it on the track that should make room (pad, guitar bus, bass) and feed the track that should get the room (vocal, lead, kick) into its **Sidechain** input. Only the frequencies the sidechain is using at that moment are turned down, by up to 24 dB.

Built from the Skell VST3 template (CMake + GitHub Actions); behaviour follows the SpaceBtw manual.

## Live Tracking vs Studio

The original high-resolution sidechain FFT processor remains the **Studio** path and the default for existing sessions. The new **Live** button selects an independently developed 10-band causal filter-bank processor shared from SkellCore:

| Processing mode | Intrinsic delay at 48 kHz | Tradeoff |
| --- | ---: | --- |
| Studio (default, original) | 2,048 samples = **42.67 ms** | Original high-resolution FFT sidechain ducking |
| Live (opt-in) | **0 added samples** | Tracking-friendly, lower spectral selectivity and a different phase/tonal response |

**Important:** zero *plugin-added* delay is not zero audible monitoring latency. FL Studio, the audio interface/ASIO driver, audio buffer, and any earlier plugins in the monitoring chain still add delay. Test with a 64/128-sample audio buffer if the PC can run it without dropouts. The previously reported ~70 ms is plausibly a combination of this plugin's Studio FFT delay and other audio-device/host buffering; that total has not been measured here.

Live keeps existing Amount, Smooth, Link, Attack, Release, band cut, Freeze and Power controls, but its filter-bank sound differs from Studio. The new `liveMode` state defaults **off** so old projects keep the old sound; no existing parameter IDs or plugin identity were changed.

The DAW must agree with the currently reported processing delay. Choose Live/Studio **while the transport is stopped**; the editor defers mode changes requested during playback until the transport stops, when it safely calls JUCE `setLatencySamples()` on the **message thread**, never in the audio callback. If a host cannot apply the change immediately, save and reload the plugin or reopen the project. The editor displays **STOP TO APPLY MODE** when a change is pending. For consistency, Live mode also applies at `prepareToPlay()` when loading a saved plugin. Do not automate this mode switch during active recording. Metering continues in both modes.

The Live algorithm uses no lookahead, FFT window waiting, allocations, locks, or host display notifications inside its per-sample processing. Its output is not mathematically the same as the existing STFT processing, so judge the tradeoff on real vocal/pad/kick sidechain sessions before choosing Live for final mixing.

## Controls
| Control | Range | Default |
| --- | --- | --- |
| Amount | 0-100 % of 24 dB | 50 % |
| Smooth | 0-100 % (0 to 1 octave spread) | 30 % |
| Stereo Link | 0-100 % (pulls each side's cut toward the deeper of L/R) | 0 % |
| Attack | 1-200 ms | 10 ms |
| Release | 10-1000 ms | 150 ms |
| Low Cut / High Cut | 20 Hz-20 kHz, kept >= 1.25x apart, soft 1/3-octave edges | open |
| Freeze | holds the current cut shape, even with no sidechain | off |
| Power | 10 ms crossfade to the (delayed) dry signal | on |

All are host parameters and can be automated.

## Behaviour
- **Studio mode:** FFT size scales with the sample rate (2048 up to 50 kHz, 4096 up to 100 kHz, 8192 above) with 75 % overlap, so frequency resolution and the ~43 ms latency stay the same at every rate. The latency (2048 / 4096 / 8192 samples) is reported to the host. Attack and release are applied once per hop (about 11 ms), so attack times below that behave as one hop.
- Level independent: the cut follows the sidechain relative to its own recent peak (none at -50 dB, full at -10 dB), so loud and quiet sidechains carve the same shape.
- Stereo: L is cut from sidechain L, R from sidechain R. A mono sidechain drives both.
- Amount 0, Power off, or no sidechain (and no Freeze) gives a bit-exact delayed copy of the input.
- Status line shows `no sidechain` when the sidechain input has been silent for 1 s.

## Build
Push this folder to GitHub, then **Actions -> Build Windows VST3 -> Run workflow** and download the artifact. The workflow runs the DSP tests first.

Local: `cmake -S . -B build -A x64 -DJUCE_PATH=C:/JUCE` then `cmake --build build --config Release --parallel 2` (without `JUCE_PATH`, JUCE 8.0.12 is fetched).

Original Studio DSP tests (no JUCE needed, run in CI): `g++ -O2 -std=c++17 -I Source tests/dsp_test.cpp -o dsp_test && ./dsp_test`
They cover exact bypass, cut depth and accuracy, level independence, band, freeze, stereo/link, mono, 44.1-192 kHz, block-size independence, click-free parameter changes, overshoot, NaN/denormal safety and CPU.

Editor test (optional, headless): `tests/ui_render.sh <JUCE dir> <prebuilt module objects> <out dir>` renders every skin at three window sizes to PNG and checks that knobs, buttons and the band slider drive the parameters, parameter text parsing, and state save/restore. The script header lists the JUCE translation units it needs.

## Look
Skell house look: warm black, bone-white kerned type, one accent colour, flat square buttons, corner-bracket cards. Click the logo (top-left) to cycle Bone / Neon / Graveyard / Clean; the choice is saved with the plugin state. The header strip lights up with the deepest current cut.

## Layout
- `SkellCore/LiveEngine.h` (immutable pinned shared dependency) - Live zero-added-delay engine and latency contract
- `Source/SpacerDSP.h` - JUCE-free Studio engine (FFT, spectral ducking, display data)
- `Source/PluginProcessor.*` - parameters, sidechain bus, state
- `Source/SkellTheme.h` - Skell house look (4 skins, bracket cards, spacer mark)
- `Source/PluginEditor.*` - UI in a 550x450 design space, resizable at fixed aspect
- `tests/dsp_test.cpp` - numeric tests

## Status
DSP and headless editor tests pass (JUCE 8.0.12 on Linux), and the plugin sources compile with no warnings under GCC and Clang at `-Wall -Wextra -Wshadow -Wconversion`. The Windows VST3 build itself has not been run here, so check the first CI build and the plugin in a real host. The plugin code is `Sk02` (manufacturer `SkAu`).
