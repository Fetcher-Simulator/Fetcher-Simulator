[CmdletBinding()]
param()

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

function Assert-Throws {
    param(
        [Parameter(Mandatory = $true)][scriptblock] $Action,
        [Parameter(Mandatory = $true)][string] $Message
    )
    $threw = $false
    try {
        & $Action
    }
    catch {
        $threw = $true
    }
    if (-not $threw) {
        throw $Message
    }
}

function Write-TestFile {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Content
    )
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Path) | Out-Null
    Set-Content -LiteralPath $Path -Value $Content -Encoding UTF8
}

function Add-ZipFile {
    param(
        [Parameter(Mandatory = $true)] $Zip,
        [Parameter(Mandatory = $true)][string] $SourcePath,
        [Parameter(Mandatory = $true)][string] $EntryName
    )
    $entry = $Zip.CreateEntry($EntryName.Replace("\", "/"), [IO.Compression.CompressionLevel]::Optimal)
    $inputStream = [IO.File]::OpenRead($SourcePath)
    try {
        $outputStream = $entry.Open()
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
}

function New-TestToolsArchive {
    param(
        [Parameter(Mandatory = $true)][string] $ArchivePath,
        [string] $UnsafeEntry = ""
    )

    $sourceRoot = Join-Path (Split-Path -Parent $ArchivePath) ("source-" + [Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $sourceRoot | Out-Null
    try {
        $payloadPaths = @(
            "FetcherLauncher.exe",
            "FetcherLauncher-THIRD-PARTY-NOTICES.txt",
            "ui/index.html",
            "ui/assets/fetcher-float.gif",
            "ui/assets/fetcher-float-right.gif",
            "ui/assets/potm2504a.jpg",
            "Update-Fetcher-Simulator.bat",
            "Update-Fetcher-Simulator.ps1",
            "Setup-Fetcher-Updater.bat",
            "Install-Fetcher-Tester-Tools.ps1",
            "Install-Fetcher-Client-Mod-Bundle.ps1",
            "Apply-Fetcher-Public-Test-Config.ps1",
            "fetcher-update-channel.json",
            "fetcher-client-protection-policy.json"
        )

        foreach ($relativePath in $payloadPaths) {
            if ($relativePath -in @("fetcher-update-channel.json", "fetcher-client-protection-policy.json")) {
                continue
            }
            $path = Join-Path $sourceRoot $relativePath.Replace("/", "\")
            Write-TestFile -Path $path -Content "test payload for $relativePath"
        }
        [IO.File]::WriteAllBytes((Join-Path $sourceRoot "FetcherLauncher.exe"), [byte[]](1, 2, 3, 4))

        $channel = [ordered]@{
            schemaVersion = 1
            channel = "vehicles"
            clientRepository = "Fetcher-Simulator/Fetcher-Simulator"
            clientReleaseTag = "Fetcher-Simulator-Vehicles"
            clientAssetName = "fetcher-simulator.zip"
        }
        $channel | ConvertTo-Json -Depth 5 |
            Set-Content -LiteralPath (Join-Path $sourceRoot "fetcher-update-channel.json") -Encoding UTF8

        $protectedPaths = @($payloadPaths | ForEach-Object { $_.Replace("\", "/").ToLowerInvariant() }) + @(
            "fetcher-client-files.json",
            "fetcher-tester-tools.json"
        )
        $policy = [ordered]@{
            schemaVersion = 1
            exactPaths = $protectedPaths
            prefixes = @("_fetcher_update/", "userdata/")
            suffixes = @(".dmp")
        }
        $policy | ConvertTo-Json -Depth 5 |
            Set-Content -LiteralPath (Join-Path $sourceRoot "fetcher-client-protection-policy.json") -Encoding UTF8

        $records = New-Object System.Collections.Generic.List[object]
        foreach ($relativePath in $payloadPaths) {
            $path = Join-Path $sourceRoot $relativePath.Replace("/", "\")
            $item = Get-Item -LiteralPath $path
            $records.Add([ordered]@{
                path = $relativePath.Replace("\", "/")
                size = [int64]$item.Length
                sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            })
        }
        $manifest = [ordered]@{
            schemaVersion = 1
            channel = "fetcher-simulator-test"
            sourceCommit = "b" * 40
            generatedAtUtc = [DateTime]::UtcNow.ToString("o")
            files = @($records | ForEach-Object { $_ })
        }
        $manifest | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath (Join-Path $sourceRoot "fetcher-tester-tools.json") -Encoding UTF8

        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        $zip = [IO.Compression.ZipFile]::Open($ArchivePath, [IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in Get-ChildItem -LiteralPath $sourceRoot -Recurse -Force -File) {
                $relativePath = $file.FullName.Substring($sourceRoot.Length).TrimStart("\", "/").Replace("\", "/")
                Add-ZipFile -Zip $zip -SourcePath $file.FullName -EntryName $relativePath
            }
            if (-not [string]::IsNullOrWhiteSpace($UnsafeEntry)) {
                $entry = $zip.CreateEntry($UnsafeEntry)
                $writer = New-Object IO.StreamWriter($entry.Open())
                try {
                    $writer.Write("unsafe")
                }
                finally {
                    $writer.Dispose()
                }
            }
        }
        finally {
            $zip.Dispose()
        }

        $item = Get-Item -LiteralPath $ArchivePath
        return [pscustomobject]@{
            Path = $item.FullName
            Size = [int64]$item.Length
            Digest = "sha256:$((Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256).Hash.ToLowerInvariant())"
        }
    }
    finally {
        if (Test-Path -LiteralPath $sourceRoot -PathType Container) {
            Remove-Item -LiteralPath $sourceRoot -Recurse -Force
        }
    }
}

function Write-ReleaseMetadata {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]] $Assets
    )
    [ordered]@{ assets = $Assets } | ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath $Path -Encoding UTF8
}

function New-InstallRoot {
    param([Parameter(Mandatory = $true)][string] $Path)
    New-Item -ItemType Directory -Force -Path $Path | Out-Null
    [IO.File]::WriteAllBytes((Join-Path $Path "openmw.exe"), [byte[]](1))
}

$scriptPath = Join-Path $PSScriptRoot "Install-FetcherTesterToolsRelease.ps1"
[void][ScriptBlock]::Create((Get-Content -LiteralPath $scriptPath -Raw))
$workRoot = Join-Path ([IO.Path]::GetTempPath()) ("fetcher-tools-embed-test-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $workRoot | Out-Null
try {
    $archive = New-TestToolsArchive -ArchivePath (Join-Path $workRoot "fetcher-tester-tools.zip")
    $metadataPath = Join-Path $workRoot "release.json"
    $asset = [pscustomobject]@{
        name = "fetcher-tester-tools.zip"
        size = $archive.Size
        digest = $archive.Digest
        browser_download_url = "https://example.invalid/fetcher-tester-tools.zip"
    }
    Write-ReleaseMetadata -Path $metadataPath -Assets @($asset)

    $installRoot = Join-Path $workRoot "install-success"
    $receiptPath = Join-Path $workRoot "verification.json"
    New-InstallRoot -Path $installRoot
    & $scriptPath -InstallDir $installRoot -ReleaseMetadataPath $metadataPath `
        -AssetArchivePath $archive.Path -VerificationReceiptPath $receiptPath

    foreach ($requiredPath in @(
        "FetcherLauncher.exe",
        "FetcherLauncher-THIRD-PARTY-NOTICES.txt",
        "ui\index.html",
        "ui\assets\fetcher-float.gif",
        "ui\assets\fetcher-float-right.gif",
        "ui\assets\potm2504a.jpg",
        "Update-Fetcher-Simulator.bat",
        "Update-Fetcher-Simulator.ps1",
        "Setup-Fetcher-Updater.bat",
        "fetcher-update-channel.json",
        "fetcher-client-protection-policy.json",
        "fetcher-tester-tools.json"
    )) {
        $path = Join-Path $installRoot $requiredPath
        Assert-True -Condition (Test-Path -LiteralPath $path -PathType Leaf) -Message "Embedded file is missing: $requiredPath"
        Assert-True -Condition ((Get-Item -LiteralPath $path).Length -gt 0) -Message "Embedded file is empty: $requiredPath"
    }
    $receipt = Get-Content -LiteralPath $receiptPath -Raw | ConvertFrom-Json
    Assert-True -Condition ([bool]$receipt.digestValidated) -Message "Successful embedding did not record digest validation."
    Assert-True -Condition ([string]$receipt.expectedDigest -eq $archive.Digest) -Message "Verification receipt expected digest mismatch."
    Assert-True -Condition ([string]$receipt.actualSha256 -eq $archive.Digest.Substring(7)) -Message "Verification receipt actual hash mismatch."

    $missingMetadata = Join-Path $workRoot "release-missing.json"
    Write-ReleaseMetadata -Path $missingMetadata -Assets @()
    Assert-Throws -Message "Embedding accepted a missing tester-tools asset." -Action {
        & $scriptPath -InstallDir $installRoot -ReleaseMetadataPath $missingMetadata -AssetArchivePath $archive.Path
    }

    $duplicateMetadata = Join-Path $workRoot "release-duplicate.json"
    Write-ReleaseMetadata -Path $duplicateMetadata -Assets @($asset, $asset)
    Assert-Throws -Message "Embedding accepted duplicate tester-tools assets." -Action {
        & $scriptPath -InstallDir $installRoot -ReleaseMetadataPath $duplicateMetadata -AssetArchivePath $archive.Path
    }

    $badDigestMetadata = Join-Path $workRoot "release-bad-digest.json"
    $badDigestAsset = $asset.PSObject.Copy()
    $badDigestAsset.digest = "sha256:not-a-digest"
    Write-ReleaseMetadata -Path $badDigestMetadata -Assets @($badDigestAsset)
    Assert-Throws -Message "Embedding accepted malformed tester-tools digest metadata." -Action {
        & $scriptPath -InstallDir $installRoot -ReleaseMetadataPath $badDigestMetadata -AssetArchivePath $archive.Path
    }

    $mismatchMetadata = Join-Path $workRoot "release-mismatch.json"
    $mismatchAsset = $asset.PSObject.Copy()
    $mismatchAsset.digest = "sha256:$('0' * 64)"
    Write-ReleaseMetadata -Path $mismatchMetadata -Assets @($mismatchAsset)
    Assert-Throws -Message "Embedding accepted a tester-tools digest mismatch." -Action {
        & $scriptPath -InstallDir $installRoot -ReleaseMetadataPath $mismatchMetadata -AssetArchivePath $archive.Path
    }

    $emptyArchivePath = Join-Path $workRoot "empty.zip"
    [IO.File]::WriteAllBytes($emptyArchivePath, [byte[]]@())
    $emptyMetadata = Join-Path $workRoot "release-empty.json"
    $emptyAsset = [pscustomobject]@{
        name = "fetcher-tester-tools.zip"
        size = 0
        digest = "sha256:$((Get-FileHash -LiteralPath $emptyArchivePath -Algorithm SHA256).Hash.ToLowerInvariant())"
        browser_download_url = "https://example.invalid/empty.zip"
    }
    Write-ReleaseMetadata -Path $emptyMetadata -Assets @($emptyAsset)
    Assert-Throws -Message "Embedding accepted an empty tester-tools asset." -Action {
        & $scriptPath -InstallDir $installRoot -ReleaseMetadataPath $emptyMetadata -AssetArchivePath $emptyArchivePath
    }

    foreach ($unsafeEntry in @("../escape.txt", "C:/escape.txt")) {
        $unsafeArchive = New-TestToolsArchive -ArchivePath (Join-Path $workRoot ("unsafe-" + [Guid]::NewGuid().ToString("N") + ".zip")) `
            -UnsafeEntry $unsafeEntry
        $unsafeMetadata = Join-Path $workRoot ("release-unsafe-" + [Guid]::NewGuid().ToString("N") + ".json")
        $unsafeAsset = [pscustomobject]@{
            name = "fetcher-tester-tools.zip"
            size = $unsafeArchive.Size
            digest = $unsafeArchive.Digest
            browser_download_url = "https://example.invalid/unsafe.zip"
        }
        Write-ReleaseMetadata -Path $unsafeMetadata -Assets @($unsafeAsset)
        Assert-Throws -Message "Embedding accepted unsafe archive path $unsafeEntry." -Action {
            & $scriptPath -InstallDir $installRoot -ReleaseMetadataPath $unsafeMetadata -AssetArchivePath $unsafeArchive.Path
        }
    }
    Assert-True -Condition (-not (Test-Path -LiteralPath (Join-Path $workRoot "escape.txt"))) `
        -Message "Unsafe tester-tools archive escaped its extraction root."

    Write-Host "Fetcher tester-tools embedding tests passed."
}
finally {
    if (Test-Path -LiteralPath $workRoot -PathType Container) {
        Remove-Item -LiteralPath $workRoot -Recurse -Force
    }
}
