# skell-spacer-vst

A Trackspacer-style **sidechain spectral ducker** (VST3, JUCE 8). Put it on the track that should make room (pad, guitar bus, bass) and feed the track that should get the room (vocal, lead, kick) into its **Sidechain** input. Only the frequencies the sidechain is using at that moment are turned down, by up to 24 dB.

Built from the Skell VST3 template (CMake + GitHub Actions); behaviour follows the SpaceBtw manual.

## Controls
| Control | Range | Default |
| --- | --- | --- |
| Amount | 0-100 % of 24 dB | 50 % |
| Smooth | 0-100 % (0 to 1 octave spread) | 30 % |
| Attack | 1-200 ms | 10 ms |
| Release | 10-1000 ms | 150 ms |
| Low Cut / High Cut | 20 Hz-20 kHz, kept >= 1.25x apart, soft 1/3-octave edges | open |
| Freeze | holds the current cut shape, even with no sidechain | off |
| Power | 10 ms crossfade to the (delayed) dry signal | on |

All are host parameters and can be automated.

## Behaviour
- 2048-point FFT, 75 % overlap, 2048-sample latency (reported to the host).
- Level independent: the cut follows the sidechain relative to its own recent peak (none at -50 dB, full at -10 dB), so loud and quiet sidechains carve the same shape.
- Stereo: L is cut from sidechain L, R from sidechain R. A mono sidechain drives both.
- Amount 0, Power off, or no sidechain (and no Freeze) gives a bit-exact delayed copy of the input.
- Status line shows `no sidechain` when the sidechain input has been silent for 1 s.

## Build
Push this folder to GitHub, then **Actions -> Build Windows VST3 -> Run workflow** and download the artifact. The workflow runs the DSP tests first.

Local: `cmake -S . -B build -A x64 -DJUCE_PATH=C:/JUCE` then `cmake --build build --config Release --parallel 2` (without `JUCE_PATH`, JUCE 8.0.12 is fetched).

DSP tests (no JUCE needed): `g++ -O2 -std=c++17 -I Source tests/dsp_test.cpp -o dsp_test && ./dsp_test`

## Look
Same identity as Skell Key: warm black, bone-white kerned type, one accent colour, flat square buttons, corner-bracket cards. Click the skull key (top-left) to cycle Bone / Neon / Graveyard / Clean; the choice is saved with the plugin state. The header strip lights up with the deepest current cut.

## Layout
- `Source/SpacerDSP.h` - JUCE-free engine (FFT, spectral ducking, display data)
- `Source/PluginProcessor.*` - parameters, sidechain bus, state
- `Source/SkellTheme.h` - Skell Key house look (4 skins, bracket cards, skull-key mark), shared with Skell Key
- `Source/PluginEditor.*` - UI in a 550x450 design space, resizable at fixed aspect
- `tests/dsp_test.cpp` - numeric tests

## Status
The DSP tests pass, and the processor and editor compile cleanly against JUCE 8.0.12 headers. A full Windows VST3 build and the editor's look have **not** been run yet, so check the first CI build and how the UI looks in a host. The plugin code is `Sk02` (manufacturer `SkAu`).
