# Spacer native Quick4
The existing real JUCE editor audit has a separate four-scene mode. Use the verified offline kit JUCE 8.0.12, isolated Debian13 graphics/audio overlay, and the source ZIP from the exact PR checkout. Verify SHA256SUMS.txt and revision.txt; GitHub's PR checkout may be a merge commit. Do not label it as the branch head without checking.
```sh
source /path/to/cache/env.sh
cmake -S . -B build-quick4 -G Ninja -DCMAKE_BUILD_TYPE=Release -DJUCE_PATH="$JUCE_PATH" -DFETCHCONTENT_FULLY_DISCONNECTED=ON -DSKELL_ENABLE_EDITOR_AUDIT=ON
cmake --build build-quick4 --target SkellSpacerUIAudit --parallel 2
xvfb-run -a build-quick4/SkellSpacerUIAudit_artefacts/Release/SkellSpacerUIAudit --quick4 /absolute/captures "$(cat /absolute/revision.txt)"
```
Capture Classic, Smart active controls, Delta active, supported alternate theme. Inspect every PNG and manifest; fix/rebuild/recapture. Do not use a generated mockup. Native UI and Windows verification pending until executed. Smart page and Delta are additive; original controls available when page is closed. Heatmap uses model overlap and protection, actual cut curve, modeled masking reduction and measured power/onset retention. No new DSP by opening page. Existing sidechain Listen has priority over Delta. Source export is not an automated screenshot workflow.
