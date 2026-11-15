# skell-spacer-vst

A Trackspacer-style **sidechain spectral ducker** (VST3, JUCE 8). Put it on the track that should make room (pad, guitar bus, bass) and feed the track that should get the room (vocal, lead, kick) into its **Sidechain** input. Only the frequencies the sidechain is using at that moment are turned down, by up to 24 dB.

Built from the Skell VST3 template (CMake + GitHub Actions); behaviour follows the SpaceBtw manual.

## How it works (zero latency)

The sidechain goes through a filter bank of 1/8-octave bands (84, up to the 0.45 x sample-rate limit); each band's level sets a cut depth, and the cut is applied to the track by a chain of minimum-phase bell filters. There is no FFT and no lookahead in the audio path, so the plugin adds **0 samples** of delay at any sample rate, and it never asks the host to change its delay compensation. An output guard holds the result to the input's recent peak, so ducking can never make a peak louder than it went in.

The DAW, audio interface/driver, buffer size and earlier plugins still add their own delay; use a small buffer for tracking.

### Handy controls
- **Listen** plays the sidechain instead of the track (10 ms crossfade) so you can hear what drives the ducking. It plays the sidechain *after* its filters, i.e. exactly what the detector hears.
- **Sidechain filter** (high-pass / low-pass, 24 dB/oct) shares the bottom slider with the band: click the title (`BAND` / `SIDECHAIN FILTER`) to switch between them; a small accent dot beside `BAND` shows that the hidden filter is active shapes what the detector hears, e.g. to stop vocal rumble or harsh sibilance from driving the ducking. Because cuts are relative to the sidechain's loudest band, removing a loud rumble also lets the quieter vocal range duck at full depth.
- **Double-click** any knob to reset it; **right-click** a knob to type a value (`65`, `30 L`, `C`, `150 ms`).
- The green **cut curve** is the real response of the ducking filters (deeper of the two sides), not an estimate.

### Pan and Mid/Side
- The sidechain is always summed to mono, so a one-sided key ducks both sides equally and the stereo image stays put.
- **Pan** (like Trackspacer): where the ducking is applied. Centre = both sides; towards an edge = only that side.
- **L/R / M/S** (click the PAN label): switches what Pan means. In M/S the track is split into mid and side (the sidechain is summed to mono), so Pan left = duck only the mid, Pan right = duck only the side.


## Controls
| Control | Range | Default |
| --- | --- | --- |
| Amount | 0-100 % of 24 dB | 50 % |
| Smooth | 0-100 % (0 to 1 octave spread) | 30 % |
| Pan | L100 - R100 (M/S mode: Mid - Side) | C |
| L/R / M/S | label button under Pan | L/R |
| Attack | 1-200 ms | 10 ms |
| Release | 10-1000 ms | 150 ms |
| Low Cut / High Cut | 20 Hz-20 kHz, kept >= 1.25x apart, soft 1/3-octave edges | open |
| Sidechain high-pass / low-pass | 20 Hz-20 kHz, 24 dB/oct, kept >= 1.25x apart; the ends read Off (bit-exact) | Off |
| Freeze | holds the current cut shape, even with no sidechain | off |
| Power | 10 ms crossfade to the (delayed) dry signal | on |

All are host parameters and can be automated.

## Behaviour
- Level independent: the cut follows the sidechain relative to its own recent peak (no cut 36 dB below the loudest band, full cut 6 dB below it), so loud and quiet sidechains carve the same shape.
- Stereo: the sidechain is summed to mono and drives both sides; Pan chooses which side is ducked.
- Amount 0, Power off, or no sidechain (and no Freeze) gives a bit-exact copy of the input.
- Status line shows `no sidechain` when the sidechain input has been silent for 1 s.

## Build
Push this folder to GitHub, then **Actions -> Build Windows VST3 -> Run workflow** and download the artifact. The workflow runs the DSP tests first.

Local: `cmake -S . -B build -A x64 -DJUCE_PATH=C:/JUCE` then `cmake --build build --config Release --parallel 2` (without `JUCE_PATH`, JUCE 8.0.12 is fetched).

DSP tests (no JUCE needed, run in CI): `g++ -O2 -std=c++17 -I Source tests/live_dsp_test.cpp -o live_dsp_test && ./live_dsp_test` (zero latency, bit-exact idle, selectivity at 44.1-192 kHz, pan, M/S, NaN safety, CPU) plus `tests/stress_test.cpp` ( 8-384 kHz, block sizes 1-65536, parameter fuzzing with hostile values, channel layouts, re-prepare, silence tails, CPU)


Editor test (optional, headless): `tests/ui_render.sh <JUCE dir> <prebuilt module objects> <out dir>` renders every skin at three window sizes to PNG and checks that knobs, buttons and the band slider drive the parameters, parameter text parsing, and state save/restore. The script header lists the JUCE translation units it needs.

## Compatibility note for previous Skell Spacer versions

The V16 filter-bank DSP replaces the earlier optional FFT Studio path. Parameter IDs
`link` and `liveMode` are kept in the host parameter tree to avoid removing old
automation/state identifiers, but the V16 mono sidechain does not implement the
old stereo-link behavior and `liveMode` no longer switches to the earlier Studio engine.
**Audio from older projects may sound different even though old IDs still load.**
Do not merge or ship this as a compatible in-place upgrade without testing
existing FL Studio projects and deciding how to migrate the old mode semantics.

## Install and first test in a DAW
1. Download the `skell-spacer-vst-Windows-VST3` artifact from the Actions run, unzip it, and copy the `Skell Spacer.vst3` folder to `C:\Program Files\Common Files\VST3`. The runtime is linked statically, so nothing else needs installing.
2. Rescan plugins in the host. Put Skell Spacer on the track that should make room, and route the key (vocal) into its **Sidechain** input. The sidechain bus is off until the host enables it, and the status line reads `NO SIDECHAIN` until audio arrives. In FL Studio, route the vocal's mixer track to the beat's track and pick it as the plugin's sidechain input in the plugin wrapper settings (exact menu names vary by FL version; not yet verified here).
3. Things worth checking: the status line turns `SIDECHAIN OK`, ducking follows the vocal, Listen plays the vocal, automation of the knobs works, and a saved project restores all settings.

The CI job also runs **pluginval** (strictness 5) on the built VST3 and uploads its log.

## Look
Skell house look: warm black, bone-white kerned type, one accent colour, flat square buttons, corner-bracket cards. Click the logo (top-left) to cycle Bone / Neon / Graveyard / Clean; the choice is saved with the plugin state. The header strip lights up with the deepest current cut.

## Layout
- `Source/ZeroLatencyEngine.h` - JUCE-free zero-latency engine (filter bank + bell chain; the FFT there only drives the display)
- `Source/SpacerDSP.h` - parameter block and the FFT used by the spectrum display
- `Source/PluginProcessor.*` - parameters, sidechain bus, state
- `Source/SkellTheme.h` - Skell house look (4 skins, bracket cards, spacer mark)
- `Source/PluginEditor.*` - UI in a 550x450 design space, resizable at fixed aspect
- `tests/` - DSP, stress and headless editor tests

## Status
DSP and headless editor tests pass (JUCE 8.0.12 on Linux), and the plugin sources compile with no warnings under GCC and Clang at `-Wall -Wextra -Wshadow -Wconversion`. The Windows VST3 build itself has not been run here, so check the first CI build (it includes a pluginval pass) and the plugin in a real host. The plugin code is `Sk02` (manufacturer `SkAu`).

## Known behaviour
- Resolution is 1/8 octave: a tone ducks the track about 24 dB at its own frequency, about 20 dB a quarter-octave away, 11 dB at half an octave and almost nothing beyond. Broadband material is ducked about 24-25 dB across the whole range.
- CPU: about 5-6 % of one core for a stereo instance at 48 kHz in the worst case (about 20 % at 192 kHz).
