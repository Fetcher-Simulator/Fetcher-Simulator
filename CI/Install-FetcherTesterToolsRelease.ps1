[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $InstallDir,
    [string] $Repository = "Skooma-Breath/Fetcher-Updater",
    [string] $ReleaseTag = "fetcher-tester-tools",
    [string] $AssetName = "fetcher-tester-tools.zip",
    [string] $GitHubApiBaseUrl = "https://api.github.com",
    [string] $ReleaseMetadataPath = "",
    [string] $AssetArchivePath = "",
    [string] $VerificationReceiptPath = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

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
        $normalizedSuffix = ([string]$suffix).ToLowerInvariant()
        if ($path.EndsWith($normalizedSuffix, [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }
    return $false
}

function Get-ReleaseMetadata {
    if (-not [string]::IsNullOrWhiteSpace($ReleaseMetadataPath)) {
        return Get-Content -LiteralPath $ReleaseMetadataPath -Raw | ConvertFrom-Json
    }

    $headers = @{
        "Accept" = "application/vnd.github+json"
        "User-Agent" = "Fetcher-Vehicles-Release-Builder"
        "X-GitHub-Api-Version" = "2022-11-28"
    }
    if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
        $headers["Authorization"] = "Bearer $($env:GITHUB_TOKEN)"
    }
    $encodedTag = [Uri]::EscapeDataString($ReleaseTag)
    $releaseUri = "$($GitHubApiBaseUrl.TrimEnd('/'))/repos/$Repository/releases/tags/$encodedTag"
    return Invoke-RestMethod -UseBasicParsing -Uri $releaseUri -Headers $headers
}

function Expand-SafeArchive {
    param(
        [Parameter(Mandatory = $true)][string] $ArchivePath,
        [Parameter(Mandatory = $true)][string] $DestinationPath
    )

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $destinationRoot = [IO.Path]::GetFullPath($DestinationPath).TrimEnd("\", "/")
    New-Item -ItemType Directory -Force -Path $destinationRoot | Out-Null
    $destinationPrefix = $destinationRoot + [IO.Path]::DirectorySeparatorChar
    $zip = [IO.Compression.ZipFile]::OpenRead($ArchivePath)
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

            $destination = [IO.Path]::GetFullPath((Join-Path $destinationRoot $relativePath.Replace("/", "\")))
            if (-not $destination.StartsWith($destinationPrefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw "Archive path escapes its extraction root: $relativePath"
            }

            if ([string]::IsNullOrEmpty($entry.Name)) {
                New-Item -ItemType Directory -Force -Path $destination | Out-Null
                continue
            }

            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
            $inputStream = $entry.Open()
            try {
                $outputStream = [IO.File]::Open($destination, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
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
        if ($fileCount -eq 0) {
            throw "Tester-tools archive contains no files."
        }
    }
    finally {
        $zip.Dispose()
    }
}

function Validate-TesterToolsPayload {
    param([Parameter(Mandatory = $true)][string] $ExtractRoot)

    $manifestPath = Join-Path $ExtractRoot "fetcher-tester-tools.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "Tester-tools archive is missing fetcher-tester-tools.json."
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int]$manifest.schemaVersion -ne 1 -or
        [string]$manifest.channel -ne "fetcher-simulator-test" -or
        [string]$manifest.sourceCommit -notmatch "^[0-9a-fA-F]{40}$" -or
        $null -eq $manifest.PSObject.Properties["files"] -or
        @($manifest.files).Count -eq 0) {
        throw "Tester-tools archive contains an unsupported manifest."
    }

    $manifestPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($record in @($manifest.files)) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        if ($relativePath.Equals("fetcher-tester-tools.json", [StringComparison]::OrdinalIgnoreCase) -or
            -not $manifestPaths.Add($relativePath)) {
            throw "Tester-tools manifest contains a duplicate or reserved path: $relativePath"
        }
        $expectedHash = [string]$record.sha256
        if ([int64]$record.size -lt 0 -or $expectedHash -notmatch "^[0-9a-fA-F]{64}$") {
            throw "Tester-tools manifest contains an invalid record: $relativePath"
        }
        $payloadPath = Join-Path $ExtractRoot $relativePath.Replace("/", "\")
        if (-not (Test-Path -LiteralPath $payloadPath -PathType Leaf) -or
            (Get-Item -LiteralPath $payloadPath).Length -ne [int64]$record.size -or
            (Get-FileHash -LiteralPath $payloadPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expectedHash.ToLowerInvariant()) {
            throw "Tester-tools payload failed manifest validation: $relativePath"
        }
    }

    $payloadPaths = @(Get-ChildItem -LiteralPath $ExtractRoot -Recurse -Force -File | ForEach-Object {
        $_.FullName.Substring($ExtractRoot.Length).TrimStart("\", "/").Replace("\", "/")
    } | Where-Object { -not $_.Equals("fetcher-tester-tools.json", [StringComparison]::OrdinalIgnoreCase) })
    if ($payloadPaths.Count -ne $manifestPaths.Count) {
        throw "Tester-tools manifest does not cover the complete archive payload."
    }
    foreach ($payloadPath in $payloadPaths) {
        if (-not $manifestPaths.Contains($payloadPath)) {
            throw "Tester-tools archive contains an unmanifested payload: $payloadPath"
        }
    }

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
        "fetcher-client-protection-policy.json"
    )
    foreach ($relativePath in $requiredFiles) {
        if (-not $manifestPaths.Contains($relativePath)) {
            throw "Tester-tools archive is missing required file: $relativePath"
        }
        $payloadPath = Join-Path $ExtractRoot $relativePath.Replace("/", "\")
        if ((Get-Item -LiteralPath $payloadPath).Length -le 0) {
            throw "Tester-tools archive contains an empty required file: $relativePath"
        }
    }

    $channel = Get-Content -LiteralPath (Join-Path $ExtractRoot "fetcher-update-channel.json") -Raw | ConvertFrom-Json
    if ([int]$channel.schemaVersion -ne 1 -or
        [string]$channel.channel -ne "vehicles" -or
        [string]$channel.clientRepository -ne "Fetcher-Simulator/Fetcher-Simulator" -or
        [string]$channel.clientReleaseTag -ne "Fetcher-Simulator-Vehicles" -or
        [string]$channel.clientAssetName -ne "fetcher-simulator.zip") {
        throw "Tester-tools archive does not contain the required Vehicles update channel."
    }

    $policy = Get-Content -LiteralPath (Join-Path $ExtractRoot "fetcher-client-protection-policy.json") -Raw | ConvertFrom-Json
    if ([int]$policy.schemaVersion -ne 1 -or
        $null -eq $policy.PSObject.Properties["exactPaths"] -or
        $null -eq $policy.PSObject.Properties["prefixes"] -or
        $null -eq $policy.PSObject.Properties["suffixes"]) {
        throw "Tester-tools archive contains an unsupported client protection policy."
    }
    foreach ($relativePath in @($manifestPaths | ForEach-Object { $_ })) {
        if (-not (Test-ProtectedPath -RelativePath $relativePath -Policy $policy)) {
            throw "Tester-tools path is not protected from managed-client cleanup: $relativePath"
        }
    }
    if (-not (Test-ProtectedPath -RelativePath "fetcher-tester-tools.json" -Policy $policy)) {
        throw "fetcher-tester-tools.json is not protected from managed-client cleanup."
    }

    return [pscustomobject]@{
        Manifest = $manifest
        ManifestPath = $manifestPath
        ManifestPaths = $manifestPaths
    }
}

$installRoot = (Resolve-Path -LiteralPath $InstallDir).Path.TrimEnd("\", "/")
if (-not (Test-Path -LiteralPath (Join-Path $installRoot "openmw.exe") -PathType Leaf)) {
    throw "Vehicles client staging directory does not contain openmw.exe: $installRoot"
}

$release = Get-ReleaseMetadata
$assets = @($release.assets | Where-Object { [string]$_.name -eq $AssetName })
if ($assets.Count -ne 1) {
    throw "Release $Repository@$ReleaseTag must contain exactly one $AssetName asset; found $($assets.Count)."
}
$asset = $assets[0]
if ([int64]$asset.size -le 0) {
    throw "Release asset $AssetName is empty."
}
$assetDigest = [string]$asset.digest
if ($assetDigest -notmatch "^sha256:([0-9a-fA-F]{64})$") {
    throw "Release asset $AssetName does not provide a valid sha256 digest."
}
$expectedHash = $Matches[1].ToLowerInvariant()

$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-vehicles-tools-" + [Guid]::NewGuid().ToString("N"))
$archivePath = Join-Path $workRoot $AssetName
$extractRoot = Join-Path $workRoot "extract"
try {
    New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
    if (-not [string]::IsNullOrWhiteSpace($AssetArchivePath)) {
        Copy-Item -LiteralPath $AssetArchivePath -Destination $archivePath -Force
    }
    else {
        $downloadUrl = [string]$asset.browser_download_url
        if ([string]::IsNullOrWhiteSpace($downloadUrl)) {
            throw "Release asset $AssetName does not provide browser_download_url."
        }
        $headers = @{
            "Accept" = "application/octet-stream"
            "User-Agent" = "Fetcher-Vehicles-Release-Builder"
        }
        if (-not [string]::IsNullOrWhiteSpace($env:GITHUB_TOKEN)) {
            $headers["Authorization"] = "Bearer $($env:GITHUB_TOKEN)"
        }
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -Headers $headers -OutFile $archivePath
    }

    if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
        throw "Tester-tools asset download did not produce a file."
    }
    $archiveItem = Get-Item -LiteralPath $archivePath
    if ($archiveItem.Length -le 0) {
        throw "Downloaded tester-tools asset is empty."
    }
    if ($archiveItem.Length -ne [int64]$asset.size) {
        throw "Tester-tools asset size mismatch. Expected $([int64]$asset.size), got $($archiveItem.Length)."
    }
    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $expectedHash) {
        throw "Tester-tools digest mismatch. Expected $expectedHash, got $actualHash."
    }

    Expand-SafeArchive -ArchivePath $archivePath -DestinationPath $extractRoot
    $validation = Validate-TesterToolsPayload -ExtractRoot $extractRoot

    foreach ($record in @($validation.Manifest.files)) {
        $relativePath = ConvertTo-SafeRelativePath -Path ([string]$record.path)
        $source = Join-Path $extractRoot $relativePath.Replace("/", "\")
        $destination = Join-Path $installRoot $relativePath.Replace("/", "\")
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $source -Destination $destination -Force
    }
    Copy-Item -LiteralPath $validation.ManifestPath -Destination (Join-Path $installRoot "fetcher-tester-tools.json") -Force

    $receipt = [ordered]@{
        schemaVersion = 1
        repository = $Repository
        releaseTag = $ReleaseTag
        assetName = $AssetName
        assetSize = [int64]$archiveItem.Length
        expectedDigest = "sha256:$expectedHash"
        actualSha256 = $actualHash
        digestValidated = $true
        sourceCommit = ([string]$validation.Manifest.sourceCommit).ToLowerInvariant()
        validatedAtUtc = [DateTime]::UtcNow.ToString("o")
    }
    if (-not [string]::IsNullOrWhiteSpace($VerificationReceiptPath)) {
        $receiptDirectory = Split-Path -Parent $VerificationReceiptPath
        if (-not [string]::IsNullOrWhiteSpace($receiptDirectory)) {
            New-Item -ItemType Directory -Force -Path $receiptDirectory | Out-Null
        }
        $receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $VerificationReceiptPath -Encoding UTF8
    }

    Write-Host "Verified and embedded $AssetName from $Repository@$ReleaseTag into $installRoot."
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
