# Skell Spacer — third-party and licensing inventory

This file is a release inventory, **not a legal clearance or grant of redistribution rights**.

## Current build inputs

- **JUCE 8.0.12** — fetched by CMake when a local `JUCE_PATH` is not supplied. The VST3 SDK integration is provided through the pinned JUCE build. Confirm the applicable upstream JUCE and SDK terms and required notices before any commercial distribution.
- **SkellCore source directory** — present in this repository tree with its own `README.md`, `SOURCE_PROVENANCE.md` and `licenses/` inventory. The current `SkellSpacer` plugin target in `CMakeLists.txt` links JUCE modules directly and does not link a SkellCore target; nevertheless, review the copied directory's provenance and licenses before distributing the repository or a source archive.
- **pluginval** — Windows validation utility downloaded by CI for testing only; it is not included in the VST3 package.
- **Python 3.12** — CI metadata-validation runtime only; it is not bundled with the VST3.

## Project-owned material

The repository does not contain a root `LICENSE` or `COPYING` file at the V17 RC source head. Confirm the intended Skell Audio source and binary license with the owner before publication. Do not assume that the absence of a license file grants permission to redistribute source or binaries.

## Metadata requiring owner approval

- VST3 vendor URL is currently empty.
- VST3 support E-Mail is currently empty.
- No approved commercial license, support address, storefront terms, price or trial terms are asserted by this preview.

Keep `build-info.json` at `PREVIEW_ONLY` and `commercial_release_eligible: false` until licensing/attribution, vendor identity, support contact, signing and storefront gates are approved. Do not fill these fields with guessed or placeholder customer-facing details.

