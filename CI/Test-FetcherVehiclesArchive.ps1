[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ArchivePath,
    [Parameter(Mandatory = $true)]
    [string] $VerificationReceiptPath
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

function Assert-True {
    param(
        [Parameter(Mandatory = $true)][bool] $Condition,
        [Parameter(Mandatory = $true)][string] $Message
    )
    if (-not $Condition) {
        throw $Message
    }
}

function ConvertTo-SafeRelativePath {
    param([Parameter(Mandatory = $true)][string] $Path)

    $normalized = $Path.Replace("\", "/")
    if ([string]::IsNullOrWhiteSpace($normalized) -or
        $normalized.StartsWith("/", [StringComparison]::Ordinal) -or
        [IO.Path]::IsPathRooted($Path) -or
        $normalized.Contains(":")) {
        throw "Archive contains an absolute or invalid path: $Path"
    }
    $segments = @($normalized.Split("/", [StringSplitOptions]::RemoveEmptyEntries))
    if ($segments.Count -eq 0) {
        throw "Archive contains an invalid path: $Path"
    }
    foreach ($segment in $segments) {
        if ($segment -eq "." -or $segment -eq "..") {
            throw "Archive path escapes its root: $Path"
        }
    }
    return ($segments -join "/")
}

function Test-ProtectedPath {
    param(
        [Parameter(Mandatory = $true)][string] $RelativePath,
        [Parameter(Mandatory = $true)] $Policy
    )

    $path = $RelativePath.Replace("\", "/").TrimStart("/").ToLowerInvariant()
    if (@($Policy.exactPaths | ForEach-Object { ([string]$_).Replace("\", "/").TrimStart("/").ToLowerInvariant() }) -contains $path) {
        return $true
    }
    foreach ($prefix in @($Policy.prefixes)) {
        $normalizedPrefix = ([string]$prefix).Replace("\", "/").TrimStart("/").ToLowerInvariant()
        if ($path.StartsWith($normalizedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    foreach ($suffix in @($Policy.suffixes)) {
        if ($path.EndsWith(([string]$suffix).ToLowerInvariant(), [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Expand-SafeArchive {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Destination
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $destinationRoot = [IO.Path]::GetFullPath($Destination).TrimEnd("\", "/")
    New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
    $destinationPrefix = $destinationRoot + [IO.Path]::DirectorySeparatorChar
    $zip = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        $seenPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        $fileCount = 0
        foreach ($entry in $zip.Entries) {
            if ([string]::IsNullOrWhiteSpace($entry.FullName)) {
                continue
            }
            $trimmedPath = $entry.FullName.TrimEnd("/", "\")
            if ([string]::IsNullOrWhiteSpace($trimmedPath)) {
                continue
            }
            $relativePath = ConvertTo-SafeRelativePath -Path $trimmedPath
            if (-not $seenPaths.Add($relativePath)) {
                throw "Archive contains a duplicate path: $relativePath"
            }
            $target = [IO.Path]::GetFullPath((Join-Path $destinationRoot $relativePath.Replace("/", "\")))
            if (-not $target.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive path escapes its extraction root: $relativePath"
            }
            if ([string]::IsNullOrEmpty($entry.Name)) {
                New-Item -ItemType Directory -Force -Path $target | Out-Null
                continue
            }
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $target) | Out-Null
            $inputStream = $entry.Open()
            try {
                $outputStream = [IO.File]::Open($target, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
                try {
                    $inputStream.CopyTo($outputStream)
                }
                finally {
                    $outputStream.Dispose()
                }
            }
            finally {
                $inputStream.Dispose()
            }
            $fileCount++
        }
        Assert-True -Condition ($fileCount -gt 0) -Message "Vehicles release archive contains no files."
    }
    finally {
        $zip.Dispose()
    }
}

$resolvedArchive = (Resolve-Path -LiteralPath $ArchivePath).Path
$resolvedReceipt = (Resolve-Path -LiteralPath $VerificationReceiptPath).Path
$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-vehicles-archive-test-" + [Guid]::NewGuid().ToString("N"))
$extractRoot = Join-Path $workRoot "client"
try {
    New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
    Expand-SafeArchive -Path $resolvedArchive -Destination $extractRoot

    $requiredFiles = @(
        "FetcherLauncher.exe",
        "FetcherLauncher-THIRD-PARTY-NOTICES.txt",
        "ui/index.html",
        "ui/assets/fetcher-float.gif",
        "ui/assets/fetcher-float-right.gif",
        "ui/assets/potm2504a.jpg",
        "Update-Fetcher-Simulator.bat",
        "Update-Fetcher-Simulator.ps1",
        "Setup-Fetcher-Updater.bat",
        "fetcher-update-channel.json",
        "fetcher-client-protection-policy.json",
        "fetcher-tester-tools.json",
        "fetcher-client-files.json"
    )
    foreach ($relativePath in $requiredFiles) {
        $path = Join-Path $extractRoot $relativePath.Replace("/", "\")
        Assert-True -Condition (Test-Path -LiteralPath $path -PathType Leaf) -Message "Vehicles archive is missing $relativePath."
        Assert-True -Condition ((Get-Item -LiteralPath $path).Length -gt 0) -Message "Vehicles archive contains empty file $relativePath."
    }

    $channel = Get-Content -LiteralPath (Join-Path $extractRoot "fetcher-update-channel.json") -Raw | ConvertFrom-Json
    Assert-True -Condition (
        [int]$channel.schemaVersion -eq 1 -and
        [string]$channel.channel -eq "vehicles" -and
        [string]$channel.clientRepository -eq "Fetcher-Simulator/Fetcher-Simulator" -and
        [string]$channel.clientReleaseTag -eq "Fetcher-Simulator-Vehicles" -and
        [string]$channel.clientAssetName -eq "fetcher-simulator.zip"
    ) -Message "Vehicles archive contains an invalid update channel."

    $policy = Get-Content -LiteralPath (Join-Path $extractRoot "fetcher-client-protection-policy.json") -Raw | ConvertFrom-Json
    Assert-True -Condition (
        [int]$policy.schemaVersion -eq 1 -and
        $null -ne $policy.PSObject.Properties["exactPaths"] -and
        $null -ne $policy.PSObject.Properties["prefixes"] -and
        $null -ne $policy.PSObject.Properties["suffixes"]
    ) -Message "Vehicles archive contains an unsupported client protection policy."

    $toolsManifestPath = Join-Path $extractRoot "fetcher-tester-tools.json"
    $toolsManifest = Get-Content -LiteralPath $toolsManifestPath -Raw | ConvertFrom-Json
    Assert-True -Condition (
        [int]$toolsManifest.schemaVersion -eq 1 -and
        [string]$toolsManifest.channel -eq "fetcher-simulator-test" -and
        [string]$toolsManifest.sourceCommit -match "^[0-9a-fA-F]{40}$" -and
        @($toolsManifest.files).Count -gt 0
    ) -Message "Vehicles archive contains an unsupported tester-tools manifest."

    $inventory = Get-Content -LiteralPath (Join-Path $extractRoot "fetcher-client-files.json") -Raw | ConvertFrom-Json
    Assert-True -Condition (
        [int]$inventory.schemaVersion -eq 1 -and
        [string]$inventory.clientCommit -match "^[0-9a-fA-F]{40}$" -and
        $null -ne $inventory.PSObject.Properties["files"]
    ) -Message "Vehicles archive contains an unsupported managed-client inventory."
    $inventoryPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($inventory.files)) {
        [void]$inventoryPaths.Add((ConvertTo-SafeRelativePath -Path ([string]$record.path)))
    }

    $toolsPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($toolsManifest.files)) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        Assert-True -Condition ($toolsPaths.Add($relativePath)) -Message "Tester-tools manifest contains duplicate path $relativePath."
        $path = Join-Path $extractRoot $relativePath.Replace("/", "\")
        Assert-True -Condition (Test-Path -LiteralPath $path -PathType Leaf) -Message "Tester-tools payload is missing from Vehicles archive: $relativePath"
        Assert-True -Condition ((Get-Item -LiteralPath $path).Length -eq [int64]$record.size) -Message "Tester-tools size mismatch in Vehicles archive: $relativePath"
        Assert-True -Condition (
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -eq ([string]$record.sha256).ToLowerInvariant()
        ) -Message "Tester-tools hash mismatch in Vehicles archive: $relativePath"
        Assert-True -Condition (Test-ProtectedPath -RelativePath $relativePath -Policy $policy) `
            -Message "Bundled tester-tool path is not protected: $relativePath"
        Assert-True -Condition (-not $inventoryPaths.Contains($relativePath)) `
            -Message "Protected tester-tool path was added to managed-client inventory: $relativePath"
    }
    Assert-True -Condition (Test-ProtectedPath -RelativePath "fetcher-tester-tools.json" -Policy $policy) `
        -Message "fetcher-tester-tools.json is not protected."
    Assert-True -Condition (-not $inventoryPaths.Contains("fetcher-tester-tools.json")) `
        -Message "fetcher-tester-tools.json was added to managed-client inventory."

    $receipt = Get-Content -LiteralPath $resolvedReceipt -Raw | ConvertFrom-Json
    Assert-True -Condition (
        [int]$receipt.schemaVersion -eq 1 -and
        [string]$receipt.repository -eq "Skooma-Breath/Fetcher-Updater" -and
        [string]$receipt.releaseTag -eq "fetcher-tester-tools" -and
        [string]$receipt.assetName -eq "fetcher-tester-tools.zip" -and
        [int64]$receipt.assetSize -gt 0 -and
        [bool]$receipt.digestValidated -and
        [string]$receipt.expectedDigest -match "^sha256:[0-9a-f]{64}$" -and
        [string]$receipt.actualSha256 -match "^[0-9a-f]{64}$" -and
        [string]$receipt.expectedDigest -eq "sha256:$([string]$receipt.actualSha256)"
    ) -Message "Tester-tools verification receipt does not prove digest validation."

    # Repackage the installed tester tools with a new manifest commit and marker,
    # then run the installed updater against that local package. This proves the
    # bundled updater remains independently capable of refreshing tester tools.
    $refreshStage = Join-Path $workRoot "refresh-stage"
    New-Item -ItemType Directory -Force -Path $refreshStage | Out-Null
    foreach ($record in @($toolsManifest.files)) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        $source = Join-Path $extractRoot $relativePath.Replace("/", "\")
        $destination = Join-Path $refreshStage $relativePath.Replace("/", "\")
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    $refreshMarker = "vehicles-archive-refresh-$([Guid]::NewGuid().ToString('N'))"
    $noticesPath = Join-Path $refreshStage "FetcherLauncher-THIRD-PARTY-NOTICES.txt"
    Add-Content -LiteralPath $noticesPath -Value $refreshMarker -Encoding UTF8
    foreach ($record in @($toolsManifest.files)) {
        if ([string]$record.path -eq "FetcherLauncher-THIRD-PARTY-NOTICES.txt") {
            $item = Get-Item -LiteralPath $noticesPath
            $record.size = [int64]$item.Length
            $record.sha256 = (Get-FileHash -LiteralPath $noticesPath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $toolsManifest.sourceCommit = "f" * 40
    $toolsManifest.generatedAtUtc = [DateTime]::UtcNow.ToString("o")
    $toolsManifest | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $refreshStage "fetcher-tester-tools.json") -Encoding UTF8
    $refreshArchive = Join-Path $workRoot "fetcher-tester-tools-refresh.zip"
    Compress-Archive -Path (Join-Path $refreshStage "*") -DestinationPath $refreshArchive -CompressionLevel Optimal

    # Normalize the expected first-run compatibility state before taking the
    # managed-client inventory baseline. The portable archive intentionally
    # ships pristine managed files, and the first updater invocation may apply
    # bundled compatibility transforms (and update their inventory records).
    # The refresh assertion below is specifically about tester-tools isolation.
    & (Join-Path $extractRoot "Update-Fetcher-Simulator.ps1") `
        -InstallRoot $extractRoot `
        -SkipTesterToolsUpdate `
        -SkipClientUpdate `
        -SkipClientModBundle `
        -SkipUmoMods `
        -SkipModPatches | Out-Null

    $inventoryHashBefore = (Get-FileHash -LiteralPath (Join-Path $extractRoot "fetcher-client-files.json") -Algorithm SHA256).Hash
    & (Join-Path $extractRoot "Update-Fetcher-Simulator.ps1") `
        -InstallRoot $extractRoot `
        -TesterToolsArchivePath $refreshArchive `
        -SkipClientUpdate `
        -SkipClientModBundle `
        -SkipUmoMods `
        -SkipModPatches | Out-Null

    Assert-True -Condition ((Get-Content -LiteralPath (Join-Path $extractRoot "FetcherLauncher-THIRD-PARTY-NOTICES.txt") -Raw).Contains($refreshMarker)) `
        -Message "Bundled updater did not refresh tester tools after installation."
    $refreshedManifest = Get-Content -LiteralPath (Join-Path $extractRoot "fetcher-tester-tools.json") -Raw | ConvertFrom-Json
    Assert-True -Condition ([string]$refreshedManifest.sourceCommit -eq ("f" * 40)) `
        -Message "Bundled updater did not install the refreshed tester-tools manifest."
    $refreshedChannel = Get-Content -LiteralPath (Join-Path $extractRoot "fetcher-update-channel.json") -Raw | ConvertFrom-Json
    Assert-True -Condition ([string]$refreshedChannel.clientReleaseTag -eq "Fetcher-Simulator-Vehicles") `
        -Message "Tester-tools refresh changed the Vehicles client channel."
    $inventoryHashAfter = (Get-FileHash -LiteralPath (Join-Path $extractRoot "fetcher-client-files.json") -Algorithm SHA256).Hash
    Assert-True -Condition ($inventoryHashAfter -eq $inventoryHashBefore) `
        -Message "Tester-tools refresh unexpectedly modified managed-client inventory."

    Write-Host "Fetcher Vehicles archive tests passed."
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
