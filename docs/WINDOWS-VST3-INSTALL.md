# Skell Spacer — Windows VST3 private-preview install

**Status: unsigned private preview only.** This package is not a commercial release and is not cleared for redistribution.

## Requirements

- Windows x64
- PowerShell 7
- A host that scans the standard per-user VST3 folder
- Close the DAW before installing or rolling back

## Validate without changing files

Extract the complete ZIP, open PowerShell 7 in the extracted folder, and run:

```powershell
pwsh -NoProfile -File .\Install-SkellSpacerPreview.ps1 -PackageRoot . -WhatIf
```

The script validates the single flat VST3 bundle, strict module identity, both expected 32-hex class IDs, AMD64 PE32+ binary, source/build provenance, pluginval strictness-5 record, and every SHA-256 entry. `-WhatIf` performs validation but does not create the install or backup directories.

## Install per-user

```powershell
pwsh -NoProfile -File .\Install-SkellSpacerPreview.ps1 -PackageRoot .
```

Default destination:

`%LOCALAPPDATA%\Programs\Common\VST3\Skell Spacer.vst3`

Default backup root:

`%LOCALAPPDATA%\Skelly\Backups\SkellSpacer`

No administrator rights, machine-wide Program Files writes, registry edits, standalone application, or DAW project modifications are performed. The installer copies and hashes an existing Skell Spacer bundle before replacing it. The new bundle is staged and hash-verified before the directory swap. The previous version remains in the separate backup root.

## Update and rollback

Use the same install command with a newer complete private-preview ZIP. The installer records the active version, path and bundle tree hash.

Preview rollback first:

```powershell
pwsh -NoProfile -File .\Install-SkellSpacerPreview.ps1 -Rollback -WhatIf
```

Restore the prior verified version:

```powershell
pwsh -NoProfile -File .\Install-SkellSpacerPreview.ps1 -Rollback
```

The current version is preserved as a forward backup. If the installed bundle was modified after installation, rollback refuses to overwrite it.

## Self-test

```powershell
pwsh -NoProfile -File .\Install-SkellSpacerPreview.ps1 -SelfTest
```

The self-test uses temporary folders and synthetic test binaries only; it does not install the plugin.

## Host acceptance still required

After installing, rescan VST3 plugins and test the actual sidechain routing in FL Studio. Verify host state, automation, save/reopen, Listen/bypass and real vocal/beat A/B. Windows build and pluginval do not imply FL Studio acceptance. V17 also does not claim sound compatibility with the old, unshipped V7 FFT Studio prototype.

