[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'High')]
param(
    [string]$PackageRoot,
    [string]$Destination,
    [string]$BackupRoot,
    [switch]$Rollback,
    [switch]$SelfTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ProductName = 'Skell Spacer'
$script:BundleName = 'Skell Spacer.vst3'
$script:ExpectedClassIds = @(
    'abcdef019182faeb536b4175536b3032',
    'abcdef011234abcd536b4175536b3032'
)
$script:FailBeforeInstallSwap = $false
$script:FailBeforeRollbackSwap = $false

function Get-FullPath {
    param([Parameter(Mandatory)][string]$Path)
    return [IO.Path]::GetFullPath($Path)
}

function Test-PathWithin {
    param([Parameter(Mandatory)][string]$Parent, [Parameter(Mandatory)][string]$Child)
    $parentFull = Get-FullPath $Parent
    $childFull = Get-FullPath $Child
    if ([string]::Equals($parentFull, $childFull, [StringComparison]::OrdinalIgnoreCase)) { return $true }
    $separator = [IO.Path]::DirectorySeparatorChar
    $prefix = if ($parentFull.EndsWith([string]$separator)) { $parentFull } else { $parentFull + $separator }
    return $childFull.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
}

function Test-PathsOverlap {
    param([Parameter(Mandatory)][string]$Left, [Parameter(Mandatory)][string]$Right)
    return (Test-PathWithin -Parent $Left -Child $Right) -or (Test-PathWithin -Parent $Right -Child $Left)
}

function Assert-NonRootPath {
    param([Parameter(Mandatory)][string]$Path)
    $full = Get-FullPath $Path
    $root = [IO.Path]::GetPathRoot($full)
    $trimmedFull = $full.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $trimmedRoot = $root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    if ([string]::Equals($trimmedFull, $trimmedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing a filesystem root: $full"
    }
}

function Assert-NoReparsePoints {
    param([Parameter(Mandatory)][string]$Path)
    $root = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if (($root.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
        throw "Refusing a reparse-point root: $Path"
    }
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $pending.Push($root.FullName)
    while ($pending.Count -gt 0) {
        $current = $pending.Pop()
        foreach ($item in (Get-ChildItem -LiteralPath $current -Force -ErrorAction Stop)) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Refusing a tree containing a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
        }
    }
}


function Assert-NoReparsePathAncestors {
    param([Parameter(Mandatory)][string]$Path)
    $full = Get-FullPath $Path
    $volumeRoot = [IO.Path]::GetPathRoot($full)
    if ([string]::IsNullOrWhiteSpace($volumeRoot)) { throw "Path is not rooted: $Path" }
    $current = $volumeRoot
    if (Test-Path -LiteralPath $current) {
        $rootItem = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing a reparse-point volume root: $current"
        }
    }
    $relative = $full.Substring($volumeRoot.Length)
    foreach ($segment in ($relative -split '[\\/]' | Where-Object { $_.Length -gt 0 })) {
        $current = Join-Path $current $segment
        if (-not (Test-Path -LiteralPath $current)) { break }
        $item = Get-Item -LiteralPath $current -Force -ErrorAction Stop
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing a path with a reparse-point ancestor: $current"
        }
    }
}

function Ensure-SafeDirectory {
    param([Parameter(Mandatory)][string]$Path)
    Assert-NonRootPath -Path $Path
    Assert-NoReparsePathAncestors -Path $Path
    if (Test-Path -LiteralPath $Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Container)) { throw "Expected a directory: $Path" }
    } else {
        $null = New-Item -ItemType Directory -Path $Path -Force
    }
    Assert-NoReparsePathAncestors -Path $Path
    Assert-NoReparsePoints -Path $Path
}

function Assert-SafeInstallPaths {
    param([string]$PackageRoot, [string]$Destination, [string]$BackupRoot)
    foreach ($path in @($PackageRoot, $Destination, $BackupRoot)) {
        Assert-NonRootPath -Path $path
        Assert-NoReparsePathAncestors -Path $path
    }
    if (Test-PathsOverlap -Left $PackageRoot -Right $Destination) { throw 'Package and install destination must not overlap' }
    if (Test-PathsOverlap -Left $PackageRoot -Right $BackupRoot) { throw 'Package and backup root must not overlap' }
    if (Test-PathsOverlap -Left $Destination -Right $BackupRoot) { throw 'Install destination and backup root must not overlap' }
}

function Get-TreeSha256 {
    param([Parameter(Mandatory)][string]$Path)
    $root = Get-FullPath $Path
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Bundle directory missing: $root" }
    Assert-NoReparsePoints -Path $root
    $files = @(Get-ChildItem -LiteralPath $root -File -Recurse -Force | Sort-Object -Property FullName)
    if ($files.Count -eq 0) { throw "Bundle contains no files: $root" }
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($file in $files) {
        $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines.Add("$hash  $relative")
    }
    $payload = [Text.Encoding]::UTF8.GetBytes(($lines -join [Environment]::NewLine) + [Environment]::NewLine)
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($payload))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha.Dispose()
    }
}

function Test-PeX64 {
    param([Parameter(Mandatory)][string]$Path)
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 512 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "VST3 module is not a valid Windows PE file: $Path"
    }
    $offset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ($offset -lt 64 -or $offset + 26 -gt $bytes.Length) { throw "Invalid PE header offset: $Path" }
    if ($bytes[$offset] -ne 0x50 -or $bytes[$offset + 1] -ne 0x45 -or
        $bytes[$offset + 2] -ne 0 -or $bytes[$offset + 3] -ne 0) {
        throw "PE signature missing: $Path"
    }
    $machine = [BitConverter]::ToUInt16($bytes, $offset + 4)
    $sections = [BitConverter]::ToUInt16($bytes, $offset + 6)
    $magic = [BitConverter]::ToUInt16($bytes, $offset + 24)
    if ($machine -ne 0x8664 -or $magic -ne 0x20b -or $sections -eq 0) {
        throw "VST3 module must be AMD64 PE32+ with at least one section: $Path"
    }
}

function Read-ModuleIdentity {
    param([Parameter(Mandatory)][string]$BundlePath)
    Assert-NoReparsePoints -Path $BundlePath
    $moduleInfoPath = Join-Path $BundlePath 'Contents/Resources/moduleinfo.json'
    $modulePath = Join-Path $BundlePath 'Contents/x86_64-win/Skell Spacer.vst3'
    if (-not (Test-Path -LiteralPath $moduleInfoPath -PathType Leaf)) { throw "VST3 moduleinfo.json missing: $BundlePath" }
    if (-not (Test-Path -LiteralPath $modulePath -PathType Leaf)) { throw "Expected AMD64 VST3 module missing: $BundlePath" }
    $modules = @(Get-ChildItem -LiteralPath $BundlePath -File -Recurse -Filter '*.vst3')
    if ($modules.Count -ne 1 -or
        -not [string]::Equals((Get-FullPath $modules[0].FullName), (Get-FullPath $modulePath), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Installed bundle must contain exactly one VST3 module at the expected Windows path'
    }
    $document = [IO.File]::ReadAllText($moduleInfoPath) | ConvertFrom-Json -AsHashtable -ErrorAction Stop
    if ($document.Name -ne $script:ProductName -or $document.Version -notmatch '^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$') {
        throw 'VST3 product identity or semantic version is invalid'
    }
    if ($document['Factory Info'].Vendor -ne 'Skell Audio') { throw 'VST3 vendor must be Skell Audio' }
    $classes = @($document.Classes)
    if ($classes.Count -ne 2) { throw 'VST3 must contain exactly one processor and one controller class' }
    $ids = [System.Collections.Generic.List[string]]::new()
    $categories = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($class in $classes) {
        if ($class.Name -ne $script:ProductName -or $class.Vendor -ne 'Skell Audio' -or $class.Version -ne $document.Version) {
            throw 'VST3 class product, vendor or version mismatch'
        }
        $cid = ([string]$class.CID).ToLowerInvariant()
        if ($cid -notmatch '^[0-9a-f]{32}$') { throw 'VST3 class CID is invalid' }
        $ids.Add($cid)
        $categories.Add([string]$class.Category) | Out-Null
    }
    if (@($ids | Select-Object -Unique).Count -ne 2 -or $categories.Count -ne 2) {
        throw 'VST3 class IDs and categories must be unique'
    }
    if (-not $categories.Contains('Audio Module Class') -or -not $categories.Contains('Component Controller Class')) {
        throw 'VST3 processor/controller categories are invalid'
    }
    $sortedIds = @($ids | Sort-Object)
    $expectedIds = @($script:ExpectedClassIds | Sort-Object)
    if (($sortedIds -join '|') -ne ($expectedIds -join '|')) {
        throw 'VST3 class IDs differ from the preserved Skell Spacer host identity'
    }
    Test-PeX64 -Path $modulePath
    return [pscustomobject]@{
        Version = [string]$document.Version
        ClassIds = @($ids)
        BinaryPath = 'Skell Spacer.vst3/Contents/x86_64-win/Skell Spacer.vst3'
        BinarySha256 = (Get-FileHash -LiteralPath $modulePath -Algorithm SHA256).Hash.ToLowerInvariant()
        BinarySize = (Get-Item -LiteralPath $modulePath).Length
        ModuleInfoPath = 'Skell Spacer.vst3/Contents/Resources/moduleinfo.json'
        ModuleInfoSha256 = (Get-FileHash -LiteralPath $moduleInfoPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

function Test-ChecksumManifest {
    param([Parameter(Mandatory)][string]$PackageRoot)
    $root = Get-FullPath $PackageRoot
    $manifestPath = Join-Path $root 'SHA256SUMS.txt'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'SHA256SUMS.txt is missing' }
    $expected = [System.Collections.Generic.Dictionary[string, string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($line in [IO.File]::ReadAllLines($manifestPath)) {
        if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw "Invalid checksum line: $line" }
        $relative = $Matches[2].Replace('\', '/')
        $parts = $relative.Split('/')
        if ($relative.StartsWith('/') -or $relative.Contains(':') -or
            $parts.Count -eq 0 -or $parts -contains '' -or $parts -contains '.' -or $parts -contains '..') {
            throw "Unsafe checksum path: $relative"
        }
        if ($expected.ContainsKey($relative)) { throw "Duplicate or case-colliding checksum path: $relative" }
        $native = $relative.Replace('/', [string][IO.Path]::DirectorySeparatorChar)
        $full = Get-FullPath (Join-Path $root $native)
        $prefix = $root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
        if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) { throw "Checksum path escapes package root: $relative" }
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) { throw "Checksummed file is missing: $relative" }
        $expected.Add($relative, $Matches[1].ToLowerInvariant())
    }
    $actualFiles = @(Get-ChildItem -LiteralPath $root -File -Recurse -Force |
        Where-Object { -not [string]::Equals((Get-FullPath $_.FullName), (Get-FullPath $manifestPath), [StringComparison]::OrdinalIgnoreCase) })
    if ($actualFiles.Count -ne $expected.Count) {
        throw "Checksum inventory mismatch: expected $($expected.Count) files, found $($actualFiles.Count)"
    }
    foreach ($file in $actualFiles) {
        $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        if (-not $expected.ContainsKey($relative)) { throw "Unlisted package file: $relative" }
        $actualHash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actualHash -ne $expected[$relative]) { throw "Checksum mismatch: $relative" }
    }
}

function Test-PreviewPackage {
    param([Parameter(Mandatory)][string]$PackageRoot)
    $root = Get-FullPath $PackageRoot
    if (-not (Test-Path -LiteralPath $root -PathType Container)) { throw "Package root is missing: $root" }
    Assert-NoReparsePathAncestors -Path $root
    Assert-NoReparsePoints -Path $root
    foreach ($required in @('build-info.json', 'SHA256SUMS.txt', 'Install-SkellSpacerPreview.ps1',
        'WINDOWS-VST3-INSTALL.md', 'THIRD_PARTY_NOTICES.md')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $required) -PathType Leaf)) {
            throw "Required unreleased-preview package file is missing: $required"
        }
    }
    $bundles = @(Get-ChildItem -LiteralPath $root -Directory -Recurse -Filter '*.vst3')
    $expectedBundle = Join-Path $root $script:BundleName
    if ($bundles.Count -ne 1 -or
        -not [string]::Equals((Get-FullPath $bundles[0].FullName), (Get-FullPath $expectedBundle), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Package must contain exactly one flat Skell Spacer.vst3 bundle'
    }
    $bundle = $bundles[0].FullName
    $forbidden = @(Get-ChildItem -LiteralPath $bundle -File -Recurse -Force |
        Where-Object { $_.Extension.ToLowerInvariant() -in @('.exe', '.zip') })
    if ($forbidden.Count -ne 0) { throw 'VST3 bundle must not contain standalone executables or nested archives' }

    $identity = Read-ModuleIdentity -BundlePath $bundle
    $buildInfo = [IO.File]::ReadAllText((Join-Path $root 'build-info.json')) | ConvertFrom-Json -AsHashtable -ErrorAction Stop
    if ($buildInfo.product -ne $script:ProductName -or $buildInfo.vendor -ne 'Skell Audio' -or
        $buildInfo.version -ne $identity.Version -or $buildInfo.format -ne 'VST3' -or
        $buildInfo.platform -ne 'windows-x64' -or $buildInfo.release_status -ne 'preview-only') {
        throw 'build-info.json product, version, platform or preview status mismatch'
    }
    if ([string]::IsNullOrWhiteSpace([string]$buildInfo.source_commit) -or
        [string]::IsNullOrWhiteSpace([string]$buildInfo.ci_checkout_commit) -or
        [string]::IsNullOrWhiteSpace([string]$buildInfo.workflow_run_id)) {
        throw 'build-info.json is missing source/CI provenance'
    }
    if ($buildInfo.validation.pluginval.result -ne 'passed' -or
        [int]$buildInfo.validation.pluginval.strictness_level -ne 5) {
        throw 'Exact staged VST3 must record pluginval strictness 5 as passed'
    }
    if ($buildInfo.binary.path -ne $identity.BinaryPath -or
        $buildInfo.binary.sha256 -ne $identity.BinarySha256 -or
        [long]$buildInfo.binary.size_bytes -ne [long]$identity.BinarySize -or
        $buildInfo.binary.architecture -ne 'AMD64' -or $buildInfo.binary.pe_format -ne 'PE32+') {
        throw 'build-info.json binary identity/hash does not match the staged VST3'
    }
    if ($buildInfo.moduleinfo.path -ne $identity.ModuleInfoPath -or
        $buildInfo.moduleinfo.sha256 -ne $identity.ModuleInfoSha256 -or
        $buildInfo.moduleinfo.strict_json -ne $true) {
        throw 'build-info.json moduleinfo identity/hash is invalid'
    }
    $manifestIds = @($buildInfo.moduleinfo.unique_class_ids | ForEach-Object { ([string]$_).ToLowerInvariant() } | Sort-Object)
    $actualIds = @($identity.ClassIds | Sort-Object)
    if (($manifestIds -join '|') -ne ($actualIds -join '|')) { throw 'build-info.json VST3 class IDs do not match moduleinfo.json' }
    Test-ChecksumManifest -PackageRoot $root
    return [pscustomobject]@{
        PackageRoot = $root
        BundlePath = $bundle
        Version = $identity.Version
        TreeSha256 = (Get-TreeSha256 -Path $bundle)
    }
}

function Test-InstalledBundle {
    param([Parameter(Mandatory)][string]$BundlePath)
    $identity = Read-ModuleIdentity -BundlePath $BundlePath
    return [pscustomobject]@{
        Version = $identity.Version
        TreeSha256 = (Get-TreeSha256 -Path $BundlePath)
    }
}

function Write-StateFile {
    param([string]$Path, [object]$State)
    $json = ConvertTo-Json -InputObject $State -Depth 8
    [IO.File]::WriteAllText($Path, $json + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
}

function Get-RollbackPlan {
    param([string]$BackupRoot, [string]$Destination)
    if (-not (Test-Path -LiteralPath $BackupRoot -PathType Container)) { throw 'Install backup root is missing' }
    Assert-NoReparsePathAncestors -Path $BackupRoot
    Assert-NoReparsePoints -Path $BackupRoot
    $backupFull = Get-FullPath $BackupRoot
    $statePath = Join-Path $backupFull 'active-install.json'
    if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) { throw 'No Skell Spacer install history exists in this backup root' }
    $state = [IO.File]::ReadAllText($statePath) | ConvertFrom-Json -AsHashtable -ErrorAction Stop
    if ($state.Schema -ne 'skellspacer-install/1' -or $state.Product -ne $script:ProductName) {
        throw 'Install state file has an unsupported schema or product identity'
    }
    $recordedDestination = Get-FullPath ([string]$state.Destination)
    Assert-NonRootPath -Path $recordedDestination
    if (Test-PathsOverlap -Left $recordedDestination -Right $backupFull) {
        throw 'Recorded VST3 destination and backup root must not overlap'
    }
    if (-not (Test-Path -LiteralPath $recordedDestination -PathType Container)) { throw 'Recorded VST3 destination is missing' }
    Assert-NoReparsePathAncestors -Path $recordedDestination
    Assert-NoReparsePoints -Path $recordedDestination
    if (-not [string]::IsNullOrWhiteSpace($Destination) -and
        -not [string]::Equals((Get-FullPath $Destination), $recordedDestination, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Requested destination does not match the recorded installation path'
    }
    $target = Join-Path $recordedDestination $script:BundleName
    if (-not [string]::Equals((Get-FullPath ([string]$state.InstalledBundle)), (Get-FullPath $target), [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Recorded installation bundle path is inconsistent'
    }
    if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw 'Installed VST3 bundle is missing; refusing rollback' }
    $current = Test-InstalledBundle -BundlePath $target
    if ($current.TreeSha256 -ne [string]$state.InstalledSha256 -or
        $current.Version -ne [string]$state.InstalledVersion) {
        throw 'Installed VST3 changed since this script installed it; refusing to overwrite user or host changes'
    }
    $previousBundle = [string]$state.PreviousBundle
    if ([string]::IsNullOrWhiteSpace($previousBundle) -or -not (Test-Path -LiteralPath $previousBundle -PathType Container)) {
        throw 'No previous Skell Spacer version backup is recorded; there is nothing to roll back to'
    }
    $previousFull = Get-FullPath $previousBundle
    $backupPrefix = $backupFull
    if (-not $backupPrefix.EndsWith([IO.Path]::DirectorySeparatorChar)) { $backupPrefix += [IO.Path]::DirectorySeparatorChar }
    if (-not $previousFull.StartsWith($backupPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Previous-version backup path is outside the configured Skell Spacer backup root'
    }
    Assert-NoReparsePoints -Path $previousFull
    $previous = Test-InstalledBundle -BundlePath $previousFull
    if ($previous.TreeSha256 -ne [string]$state.PreviousSha256 -or
        $previous.Version -ne [string]$state.PreviousVersion) {
        throw 'Previous-version backup hash or version does not match install history'
    }
    return [pscustomobject]@{
        State = $state
        StatePath = $statePath
        Destination = $recordedDestination
        Target = $target
        CurrentVersion = $current.Version
        CurrentSha256 = $current.TreeSha256
        PreviousBundle = $previousFull
        PreviousVersion = $previous.Version
        PreviousSha256 = $previous.TreeSha256
    }
}

function Invoke-InstallInternal {
    param([string]$PackageRoot, [string]$Destination, [string]$BackupRoot)
    $package = Test-PreviewPackage -PackageRoot $PackageRoot
    $destinationFull = Get-FullPath $Destination
    $backupFull = Get-FullPath $BackupRoot
    Assert-SafeInstallPaths -PackageRoot $package.PackageRoot -Destination $destinationFull -BackupRoot $backupFull
    Ensure-SafeDirectory -Path $destinationFull
    Ensure-SafeDirectory -Path $backupFull

    $target = Join-Path $destinationFull $script:BundleName
    $id = [guid]::NewGuid().ToString('N')
    $previousBundle = ''
    $previousHash = ''
    $previousVersion = ''
    if (Test-Path -LiteralPath $target) {
        if (-not (Test-Path -LiteralPath $target -PathType Container)) { throw 'A non-directory item already occupies the Skell Spacer VST3 destination' }
        $previous = Test-InstalledBundle -BundlePath $target
        if ($previous.TreeSha256 -eq $package.TreeSha256) {
            Write-Host "Already installed: Skell Spacer $($package.Version). No files changed."
            return
        }
        $backupDir = Join-Path $backupFull ("previous-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $id)
        $null = New-Item -ItemType Directory -Path $backupDir
        try {
            Copy-Item -LiteralPath $target -Destination $backupDir -Recurse
            $previousBundle = Join-Path $backupDir $script:BundleName
            if ((Get-TreeSha256 -Path $previousBundle) -ne $previous.TreeSha256) {
                throw 'Previous VST3 backup verification failed; original installation was not touched'
            }
            $previousHash = $previous.TreeSha256
            $previousVersion = $previous.Version
        } catch {
            if (Test-Path -LiteralPath $backupDir) { Remove-Item -LiteralPath $backupDir -Recurse -Force }
            throw
        }
    }

    $stageRoot = Join-Path $destinationFull ('.skellspacer-stage-' + $id)
    $swapPath = Join-Path $destinationFull ('.skellspacer-previous-' + $id)
    $statePath = Join-Path $backupFull 'active-install.json'
    $stateTemp = Join-Path $backupFull ('active-install.' + $id + '.tmp')
    $null = New-Item -ItemType Directory -Path $stageRoot
    $newMoved = $false
    $oldMoved = $false
    try {
        Copy-Item -LiteralPath $package.BundlePath -Destination $stageRoot -Recurse
        $stageBundle = Join-Path $stageRoot $script:BundleName
        if ((Get-TreeSha256 -Path $stageBundle) -ne $package.TreeSha256) { throw 'Staged VST3 verification failed' }
        $state = [ordered]@{
            Schema = 'skellspacer-install/1'
            Product = $script:ProductName
            Destination = $destinationFull
            InstalledBundle = $target
            InstalledVersion = $package.Version
            InstalledSha256 = $package.TreeSha256
            PreviousBundle = $previousBundle
            PreviousVersion = $previousVersion
            PreviousSha256 = $previousHash
            UpdatedUtc = (Get-Date).ToUniversalTime().ToString('o')
        }
        Write-StateFile -Path $stateTemp -State $state
        if ($script:FailBeforeInstallSwap) { throw 'Injected self-test failure before install swap' }
        if (Test-Path -LiteralPath $target) {
            Move-Item -LiteralPath $target -Destination $swapPath
            $oldMoved = $true
        }
        Move-Item -LiteralPath $stageBundle -Destination $target
        $newMoved = $true
        if ((Get-TreeSha256 -Path $target) -ne $package.TreeSha256) { throw 'Installed VST3 hash does not match the validated package' }
        Move-Item -LiteralPath $stateTemp -Destination $statePath -Force
    } catch {
        if ($newMoved -and (Test-Path -LiteralPath $target)) {
            $failedDir = Join-Path $backupFull ('failed-install-' + $id)
            $null = New-Item -ItemType Directory -Path $failedDir -Force
            Move-Item -LiteralPath $target -Destination $failedDir
        }
        if ($oldMoved -and (Test-Path -LiteralPath $swapPath) -and -not (Test-Path -LiteralPath $target)) {
            Move-Item -LiteralPath $swapPath -Destination $target
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
        if (Test-Path -LiteralPath $stateTemp) { Remove-Item -LiteralPath $stateTemp -Force }
    }
    if (Test-Path -LiteralPath $swapPath) {
        try { Remove-Item -LiteralPath $swapPath -Recurse -Force }
        catch { Write-Warning "Temporary swap directory remains at $swapPath; verified backup is preserved." }
    }
    Write-Host "Installed Skell Spacer $($package.Version) to $target"
    if ($previousBundle) { Write-Host "Verified previous version backup: $previousBundle" }
    else { Write-Host 'No previous version existed; rollback to an older version is not available.' }
}

function Invoke-RollbackInternal {
    param([string]$BackupRoot, [string]$Destination)
    $plan = Get-RollbackPlan -BackupRoot $BackupRoot -Destination $Destination
    $id = [guid]::NewGuid().ToString('N')
    $stageRoot = Join-Path $plan.Destination ('.skellspacer-rollback-stage-' + $id)
    $forwardDir = Join-Path (Get-FullPath $BackupRoot) ('rolled-forward-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + $id)
    $forwardBundle = Join-Path $forwardDir $script:BundleName
    $stateTemp = Join-Path (Get-FullPath $BackupRoot) ('active-install.' + $id + '.tmp')
    $null = New-Item -ItemType Directory -Path $stageRoot
    $null = New-Item -ItemType Directory -Path $forwardDir
    $currentMoved = $false
    $restoredMoved = $false
    try {
        Copy-Item -LiteralPath $plan.PreviousBundle -Destination $stageRoot -Recurse
        $stageBundle = Join-Path $stageRoot $script:BundleName
        if ((Get-TreeSha256 -Path $stageBundle) -ne $plan.PreviousSha256) { throw 'Rollback staging hash mismatch' }
        $newState = [ordered]@{
            Schema = 'skellspacer-install/1'
            Product = $script:ProductName
            Destination = $plan.Destination
            InstalledBundle = $plan.Target
            InstalledVersion = $plan.PreviousVersion
            InstalledSha256 = $plan.PreviousSha256
            PreviousBundle = $forwardBundle
            PreviousVersion = $plan.CurrentVersion
            PreviousSha256 = $plan.CurrentSha256
            UpdatedUtc = (Get-Date).ToUniversalTime().ToString('o')
        }
        Write-StateFile -Path $stateTemp -State $newState
        if ($script:FailBeforeRollbackSwap) { throw 'Injected self-test failure before rollback swap' }
        Move-Item -LiteralPath $plan.Target -Destination $forwardDir
        $currentMoved = $true
        Move-Item -LiteralPath $stageBundle -Destination $plan.Target
        $restoredMoved = $true
        if ((Get-TreeSha256 -Path $plan.Target) -ne $plan.PreviousSha256) { throw 'Restored VST3 hash mismatch' }
        Move-Item -LiteralPath $stateTemp -Destination $plan.StatePath -Force
    } catch {
        if ($restoredMoved -and (Test-Path -LiteralPath $plan.Target)) {
            $failedDir = Join-Path (Get-FullPath $BackupRoot) ('failed-rollback-' + $id)
            $null = New-Item -ItemType Directory -Path $failedDir -Force
            Move-Item -LiteralPath $plan.Target -Destination $failedDir
        }
        if ($currentMoved -and (Test-Path -LiteralPath $forwardBundle) -and -not (Test-Path -LiteralPath $plan.Target)) {
            Move-Item -LiteralPath $forwardBundle -Destination $plan.Target
        }
        throw
    } finally {
        if (Test-Path -LiteralPath $stageRoot) { Remove-Item -LiteralPath $stageRoot -Recurse -Force }
        if (Test-Path -LiteralPath $stateTemp) { Remove-Item -LiteralPath $stateTemp -Force }
    }
    Write-Host "Restored Skell Spacer $($plan.PreviousVersion). Newer version preserved at $forwardBundle"
}

function Write-TestPe {
    param([string]$Path, [UInt16]$Machine = 0x8664)
    $bytes = New-Object byte[] 1024
    $bytes[0] = 0x4d
    $bytes[1] = 0x5a
    [BitConverter]::GetBytes([int]0x80).CopyTo($bytes, 0x3c)
    $bytes[0x80] = 0x50
    $bytes[0x81] = 0x45
    $bytes[0x82] = 0
    $bytes[0x83] = 0
    [BitConverter]::GetBytes([UInt16]$Machine).CopyTo($bytes, 0x84)
    [BitConverter]::GetBytes([UInt16]1).CopyTo($bytes, 0x86)
    [BitConverter]::GetBytes([UInt16]0x20b).CopyTo($bytes, 0x98)
    [IO.File]::WriteAllBytes($Path, $bytes)
}

function Write-TestModuleInfo {
    param([string]$BundlePath, [string]$Version, [switch]$DuplicateIds, [switch]$ExtraClass)
    $secondId = if ($DuplicateIds) { 'ABCDEF019182FAEB536B4175536B3032' } else { 'ABCDEF011234ABCD536B4175536B3032' }
    $classes = @(
        [ordered]@{ CID = 'ABCDEF019182FAEB536B4175536B3032'; Category = 'Audio Module Class'; Name = $script:ProductName; Vendor = 'Skell Audio'; Version = $Version },
        [ordered]@{ CID = $secondId; Category = 'Component Controller Class'; Name = $script:ProductName; Vendor = 'Skell Audio'; Version = $Version }
    )
    if ($ExtraClass) {
        $classes += [ordered]@{ CID = 'ABCDEF019182FAEB536B4175536B3032'; Category = 'Factory Preset Class'; Name = $script:ProductName; Vendor = 'Skell Audio'; Version = $Version }
    }
    $document = [ordered]@{
        Name = $script:ProductName
        Version = $Version
        'Factory Info' = [ordered]@{ Vendor = 'Skell Audio' }
        Classes = $classes
    }
    $path = Join-Path $BundlePath 'Contents/Resources/moduleinfo.json'
    [IO.File]::WriteAllText($path, (ConvertTo-Json -InputObject $document -Depth 8), [Text.UTF8Encoding]::new($false))
}

function Write-TestBundle {
    param([string]$Root, [string]$Version, [switch]$DuplicateIds, [switch]$ExtraClass, [UInt16]$Machine = 0x8664)
    $bundle = Join-Path $Root $script:BundleName
    $moduleDir = Join-Path $bundle 'Contents/x86_64-win'
    $resourceDir = Join-Path $bundle 'Contents/Resources'
    $null = New-Item -ItemType Directory -Path $moduleDir -Force
    $null = New-Item -ItemType Directory -Path $resourceDir -Force
    Write-TestModuleInfo -BundlePath $bundle -Version $Version -DuplicateIds:$DuplicateIds -ExtraClass:$ExtraClass
    Write-TestPe -Path (Join-Path $moduleDir $script:BundleName) -Machine $Machine
    return $bundle
}

function Write-TestChecksumManifest {
    param([string]$Root)
    $manifest = Join-Path $Root 'SHA256SUMS.txt'
    $lines = [System.Collections.Generic.List[string]]::new()
    foreach ($file in (Get-ChildItem -LiteralPath $Root -File -Recurse -Force |
        Where-Object { $_.FullName -ne $manifest } | Sort-Object -Property FullName)) {
        $relative = [IO.Path]::GetRelativePath($Root, $file.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $lines.Add("$hash  $relative")
    }
    [IO.File]::WriteAllLines($manifest, $lines.ToArray(), [Text.UTF8Encoding]::new($false))
}

function Write-TestPackage {
    param([string]$Root, [string]$Version)
    $null = New-Item -ItemType Directory -Path $Root -Force
    $bundle = Write-TestBundle -Root $Root -Version $Version
    $module = Join-Path $bundle 'Contents/x86_64-win/Skell Spacer.vst3'
    $moduleInfo = Join-Path $bundle 'Contents/Resources/moduleinfo.json'
    $buildInfo = [ordered]@{
        schema_version = 1
        product = $script:ProductName
        version = $Version
        vendor = 'Skell Audio'
        format = 'VST3'
        platform = 'windows-x64'
        source_commit = 'self-test-source'
        ci_checkout_commit = 'self-test-ci'
        workflow_run_id = '1'
        binary = [ordered]@{
            path = 'Skell Spacer.vst3/Contents/x86_64-win/Skell Spacer.vst3'
            size_bytes = (Get-Item -LiteralPath $module).Length
            sha256 = (Get-FileHash -LiteralPath $module -Algorithm SHA256).Hash.ToLowerInvariant()
            architecture = 'AMD64'
            pe_format = 'PE32+'
        }
        moduleinfo = [ordered]@{
            path = 'Skell Spacer.vst3/Contents/Resources/moduleinfo.json'
            sha256 = (Get-FileHash -LiteralPath $moduleInfo -Algorithm SHA256).Hash.ToLowerInvariant()
            strict_json = $true
            unique_class_ids = @($script:ExpectedClassIds)
            normalization_applied = $false
        }
        validation = [ordered]@{
            bundle_structure = 'passed'
            metadata_identity = 'passed'
            pe32_plus_amd64 = 'passed'
            pluginval = [ordered]@{ strictness_level = 5; result = 'passed' }
        }
        release_status = 'preview-only'
        commercial_metadata_gate = [ordered]@{ status = 'blocked'; blockers = @('self-test') }
        release_blockers = @('self-test')
    }
    [IO.File]::WriteAllText((Join-Path $Root 'build-info.json'),
        (ConvertTo-Json -InputObject $buildInfo -Depth 12), [Text.UTF8Encoding]::new($false))
    $scriptCopy = Join-Path $Root 'Install-SkellSpacerPreview.ps1'
    if (-not [string]::Equals((Get-FullPath $PSCommandPath), (Get-FullPath $scriptCopy), [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -LiteralPath $PSCommandPath -Destination $scriptCopy -Force
    }
    [IO.File]::WriteAllText((Join-Path $Root 'WINDOWS-VST3-INSTALL.md'), 'Self-test install guide', [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $Root 'THIRD_PARTY_NOTICES.md'), 'Self-test notices', [Text.UTF8Encoding]::new($false))
    Write-TestChecksumManifest -Root $Root
}

function Assert-Test {
    param([bool]$Condition, [string]$Label)
    if (-not $Condition) { throw "Installer self-test failed: $Label" }
    $script:SelfTestAssertions++
}

function Assert-TestFails {
    param([scriptblock]$Action, [string]$Label)
    $failed = $false
    try { & $Action | Out-Null } catch { $failed = $true }
    Assert-Test -Condition $failed -Label $Label
}

function Invoke-SelfTest {
    $script:SelfTestAssertions = 0
    $root = Join-Path ([IO.Path]::GetTempPath()) ('skellspacer-installer-test-' + [guid]::NewGuid().ToString('N'))
    $package = Join-Path $root 'package'
    try {
        Write-TestPackage -Root $package -Version '0.9.1'
        $valid = Test-PreviewPackage -PackageRoot $package
        Assert-Test -Condition ($valid.Version -eq '0.9.1') -Label 'valid preview package accepted'

        $tamper = Join-Path $root 'tamper'
        Write-TestPackage -Root $tamper -Version '0.9.1'
        [IO.File]::WriteAllText((Join-Path $tamper 'WINDOWS-VST3-INSTALL.md'), 'tampered', [Text.UTF8Encoding]::new($false))
        Assert-TestFails -Action { Test-PreviewPackage -PackageRoot $tamper } -Label 'tampered sidecar rejected'

        $traversal = Join-Path $root 'traversal'
        Write-TestPackage -Root $traversal -Version '0.9.1'
        [IO.File]::WriteAllText((Join-Path $traversal 'SHA256SUMS.txt'), ('0' * 64) + '  ../outside' + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
        Assert-TestFails -Action { Test-PreviewPackage -PackageRoot $traversal } -Label 'checksum path traversal rejected'

        $junctionTarget = Join-Path $root 'junction-target'
        $junctionLink = Join-Path $root 'junction-link'
        $null = New-Item -ItemType Directory -Path $junctionTarget -Force
        $null = New-Item -ItemType Junction -Path $junctionLink -Target $junctionTarget -ErrorAction Stop
        Assert-TestFails -Action {
            Assert-SafeInstallPaths -PackageRoot $package -Destination (Join-Path $junctionLink 'VST3') -BackupRoot (Join-Path $root 'junction-backups')
        } -Label 'reparse-point parent path rejected'
        Remove-Item -LiteralPath $junctionLink -Force

        $identityRoot = Join-Path $root 'identity'
        $identityBundle = Write-TestBundle -Root $identityRoot -Version '0.9.1' -DuplicateIds
        Assert-TestFails -Action { Read-ModuleIdentity -BundlePath $identityBundle } -Label 'duplicate VST3 class IDs rejected'
        Write-TestModuleInfo -BundlePath $identityBundle -Version '0.9.1' -ExtraClass
        Assert-TestFails -Action { Read-ModuleIdentity -BundlePath $identityBundle } -Label 'unexpected VST3 class rejected'
        Write-TestModuleInfo -BundlePath $identityBundle -Version '0.9.1'
        $identityModule = Join-Path $identityBundle 'Contents/x86_64-win/Skell Spacer.vst3'
        Write-TestPe -Path $identityModule -Machine 0xaa64
        Assert-TestFails -Action { Read-ModuleIdentity -BundlePath $identityBundle } -Label 'non-AMD64 module rejected'

        $freshDestination = Join-Path $root 'FreshVST3'
        $freshBackup = Join-Path $root 'FreshBackups'
        Invoke-InstallInternal -PackageRoot $package -Destination $freshDestination -BackupRoot $freshBackup
        $fresh = Test-InstalledBundle -BundlePath (Join-Path $freshDestination $script:BundleName)
        Assert-Test -Condition ($fresh.Version -eq '0.9.1') -Label 'fresh per-user install succeeds'
        $freshHash = $fresh.TreeSha256
        Invoke-InstallInternal -PackageRoot $package -Destination $freshDestination -BackupRoot $freshBackup
        $idempotent = Test-InstalledBundle -BundlePath (Join-Path $freshDestination $script:BundleName)
        Assert-Test -Condition ($idempotent.TreeSha256 -eq $freshHash) -Label 'same package install is idempotent'

        $updateDestination = Join-Path $root 'UpdateVST3'
        $updateBackup = Join-Path $root 'UpdateBackups'
        $null = New-Item -ItemType Directory -Path $updateDestination -Force
        $null = Write-TestBundle -Root $updateDestination -Version '0.9.0'
        $script:FailBeforeInstallSwap = $true
        Assert-TestFails -Action { Invoke-InstallInternal -PackageRoot $package -Destination $updateDestination -BackupRoot $updateBackup } -Label 'pre-swap update failure is reported'
        $script:FailBeforeInstallSwap = $false
        $beforeSwap = Test-InstalledBundle -BundlePath (Join-Path $updateDestination $script:BundleName)
        Assert-Test -Condition ($beforeSwap.Version -eq '0.9.0') -Label 'pre-swap update failure preserves old version'

        Invoke-InstallInternal -PackageRoot $package -Destination $updateDestination -BackupRoot $updateBackup
        $updated = Test-InstalledBundle -BundlePath (Join-Path $updateDestination $script:BundleName)
        Assert-Test -Condition ($updated.Version -eq '0.9.1') -Label 'update installs new version'
        $state = [IO.File]::ReadAllText((Join-Path $updateBackup 'active-install.json')) | ConvertFrom-Json -AsHashtable
        Assert-Test -Condition (Test-Path -LiteralPath $state.PreviousBundle -PathType Container) -Label 'verified previous version backup retained'

        $script:FailBeforeRollbackSwap = $true
        Assert-TestFails -Action { Invoke-RollbackInternal -BackupRoot $updateBackup -Destination $updateDestination } -Label 'pre-swap rollback failure is reported'
        $script:FailBeforeRollbackSwap = $false
        $beforeRollback = Test-InstalledBundle -BundlePath (Join-Path $updateDestination $script:BundleName)
        Assert-Test -Condition ($beforeRollback.Version -eq '0.9.1') -Label 'pre-swap rollback failure preserves current version'

        Invoke-RollbackInternal -BackupRoot $updateBackup -Destination $updateDestination
        $restored = Test-InstalledBundle -BundlePath (Join-Path $updateDestination $script:BundleName)
        Assert-Test -Condition ($restored.Version -eq '0.9.0') -Label 'rollback restores previous version'
        Invoke-RollbackInternal -BackupRoot $updateBackup -Destination $updateDestination
        $forward = Test-InstalledBundle -BundlePath (Join-Path $updateDestination $script:BundleName)
        Assert-Test -Condition ($forward.Version -eq '0.9.1') -Label 'rollback is reversible and preserves newer version'

        $module = Join-Path $updateDestination 'Skell Spacer.vst3/Contents/x86_64-win/Skell Spacer.vst3'
        $bytes = [IO.File]::ReadAllBytes($module)
        $bytes[300] = $bytes[300] -bxor 1
        [IO.File]::WriteAllBytes($module, $bytes)
        $tamperedHash = Get-TreeSha256 -Path (Join-Path $updateDestination $script:BundleName)
        Assert-TestFails -Action { Invoke-RollbackInternal -BackupRoot $updateBackup -Destination $updateDestination } -Label 'modified installed bundle blocks rollback'
        Assert-Test -Condition ((Get-TreeSha256 -Path (Join-Path $updateDestination $script:BundleName)) -eq $tamperedHash) -Label 'refused rollback leaves modified bundle untouched'

        $invalidDestination = Join-Path $root 'InvalidVST3'
        $null = New-Item -ItemType Directory -Path $invalidDestination -Force
        $invalidTarget = Join-Path $invalidDestination $script:BundleName
        [IO.File]::WriteAllText($invalidTarget, 'preserve', [Text.UTF8Encoding]::new($false))
        Assert-TestFails -Action { Invoke-InstallInternal -PackageRoot $package -Destination $invalidDestination -BackupRoot (Join-Path $root 'InvalidBackups') } -Label 'non-directory existing target rejected'
        Assert-Test -Condition ([IO.File]::ReadAllText($invalidTarget) -eq 'preserve') -Label 'invalid existing target remains untouched'

        Write-Host "PASS: $script:SelfTestAssertions Skell Spacer installer/update/rollback assertions"
    } finally {
        if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
    }
}

if ($SelfTest) {
    if ($Rollback -or -not [string]::IsNullOrWhiteSpace($PackageRoot) -or
        -not [string]::IsNullOrWhiteSpace($Destination) -or
        -not [string]::IsNullOrWhiteSpace($BackupRoot)) {
        throw '-SelfTest cannot be combined with install or rollback arguments'
    }
    Invoke-SelfTest
    return
}

if ([string]::IsNullOrWhiteSpace($BackupRoot)) {
    if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { throw 'LOCALAPPDATA is unavailable; pass -BackupRoot explicitly' }
    $BackupRoot = Join-Path $env:LOCALAPPDATA 'Skelly/Backups/Skell Spacer'
}
$BackupRoot = Get-FullPath $BackupRoot

if ($Rollback) {
    $plan = Get-RollbackPlan -BackupRoot $BackupRoot -Destination $Destination
    if ($PSCmdlet.ShouldProcess($plan.Target, "Restore verified Skell Spacer $($plan.PreviousVersion)")) {
        Invoke-RollbackInternal -BackupRoot $BackupRoot -Destination $plan.Destination
    } else {
        Write-Host 'Rollback validation passed. No files changed.'
    }
} else {
    if ([string]::IsNullOrWhiteSpace($PackageRoot)) { throw 'Pass -PackageRoot to install, or -Rollback to restore the previous version' }
    if ([string]::IsNullOrWhiteSpace($Destination)) {
        if ([string]::IsNullOrWhiteSpace($env:LOCALAPPDATA)) { throw 'LOCALAPPDATA is unavailable; pass -Destination explicitly' }
        $Destination = Join-Path $env:LOCALAPPDATA 'Programs/Common/VST3'
    }
    $package = Test-PreviewPackage -PackageRoot $PackageRoot
    $destinationFull = Get-FullPath $Destination
    Assert-SafeInstallPaths -PackageRoot $package.PackageRoot -Destination $destinationFull -BackupRoot $BackupRoot
    $target = Join-Path $destinationFull $script:BundleName
    if ($PSCmdlet.ShouldProcess($target, "Install validated Skell Spacer $($package.Version) with verified backup/rollback")) {
        Invoke-InstallInternal -PackageRoot $PackageRoot -Destination $Destination -BackupRoot $BackupRoot
    } else {
        Write-Host 'Package validation passed. No files changed.'
    }
}
