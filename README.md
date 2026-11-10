# skell-spacer-vst

A Trackspacer-style **sidechain spectral ducker** (VST3, JUCE 8). Put it on the track that should make room (pad, guitar bus, bass) and feed the track that should get the room (vocal, lead, kick) into its **Sidechain** input. Only the frequencies the sidechain is using at that moment are turned down, by up to 24 dB.

Built from the Skell VST3 template (CMake + GitHub Actions); behaviour follows the SpaceBtw manual.

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
- FFT size scales with the sample rate (2048 up to 50 kHz, 4096 up to 100 kHz, 8192 above) with 75 % overlap, so frequency resolution and the ~43 ms latency stay the same at every rate. The latency (2048 / 4096 / 8192 samples) is reported to the host. Attack and release are applied once per hop (about 11 ms), so attack times below that behave as one hop.
- Level independent: the cut follows the sidechain relative to its own recent peak (none at -50 dB, full at -10 dB), so loud and quiet sidechains carve the same shape.
- Stereo: L is cut from sidechain L, R from sidechain R. A mono sidechain drives both.
- Amount 0, Power off, or no sidechain (and no Freeze) gives a bit-exact delayed copy of the input.
- Status line shows `no sidechain` when the sidechain input has been silent for 1 s.

## Build
Push this folder to GitHub, then **Actions -> Build Windows VST3 -> Run workflow** and download the artifact. The workflow runs the DSP tests first.

Local: `cmake -S . -B build -A x64 -DJUCE_PATH=C:/JUCE` then `cmake --build build --config Release --parallel 2` (without `JUCE_PATH`, JUCE 8.0.12 is fetched).

DSP tests (no JUCE needed, run in CI): `g++ -O2 -std=c++17 -I Source tests/dsp_test.cpp -o dsp_test && ./dsp_test`
They cover exact bypass, cut depth and accuracy, level independence, band, freeze, stereo/link, mono, 44.1-192 kHz, block-size independence, click-free parameter changes, overshoot, NaN/denormal safety and CPU.

Editor test (optional, headless): `tests/ui_render.sh <JUCE dir> <prebuilt module objects> <out dir>` renders every skin at three window sizes to PNG and checks that knobs, buttons and the band slider drive the parameters, parameter text parsing, and state save/restore. The script header lists the JUCE translation units it needs.

## Look
Skell house look: warm black, bone-white kerned type, one accent colour, flat square buttons, corner-bracket cards. Click the logo (top-left) to cycle Bone / Neon / Graveyard / Clean; the choice is saved with the plugin state. The header strip lights up with the deepest current cut.

## Layout
- `Source/SpacerDSP.h` - JUCE-free engine (FFT, spectral ducking, display data)
- `Source/PluginProcessor.*` - parameters, sidechain bus, state
- `Source/SkellTheme.h` - Skell house look (4 skins, bracket cards, spacer mark)
- `Source/PluginEditor.*` - UI in a 550x450 design space, resizable at fixed aspect
- `tests/dsp_test.cpp` - numeric tests

## Status
DSP and headless editor tests pass (JUCE 8.0.12 on Linux), and the plugin sources compile with no warnings under GCC and Clang at `-Wall -Wextra -Wshadow -Wconversion`. The Windows VST3 build itself has not been run here, so check the first CI build and the plugin in a real host. The plugin code is `Sk02` (manufacturer `SkAu`).
