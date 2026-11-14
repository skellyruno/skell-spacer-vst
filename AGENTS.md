# SKELLY — REAL LOCAL JUCE UI SCREENSHOTS (Quick4)

When asked **"current UI"** / **"show the UI"** / **"screenshot"**, the image must be of the **actual compiled JUCE AudioProcessorEditor from this repository's latest C++ source**. Do not generate a concept image, render a WAV audio tool, or relabel an old CI screenshot as current. Check the exact repository and commit first.

**Shared authoritative workflow:** `skellyruno/skellcore-vst-template/docs/PLUGIN_QUICK4_ROLLOUT.md`
**Shared local builder/router:** `skellyruno/skellcore-vst-template/tools/quick4_projects.py`

Default **four** offscreen real JUCE screenshots: main, meaningful control/menu state, active state, and one alternate real color or state. `--full` is optional for bigger responsive/theming audits. Never invent unsupported skins and don't describe a selected dropdown as an open native popup.

**Migration status for THIS repo:** not yet verified with the shared native Quick4 renderer. Before claiming it works, implement a separate opt-in real-editor renderer target that instantiates the actual processor/editor, compile it locally with pinned JUCE/SkellCore, validate the four PNG files and manifest, inspect visual differences, and document the exact current commit. If blocked, explain it explicitly and ask for source/build environment only if not already accessible; never fabricate the current UI.

Keep Windows VST3 build, FL Studio loading, automation, DSP, latency, and CPU validation independently required. **No automatic GitHub Actions screenshot workflow** for everyday UI checks. GitHub can remain source control and independent Windows build fallback.
