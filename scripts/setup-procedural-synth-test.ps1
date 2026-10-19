[CmdletBinding()]
param(
    [string] $SimulatorRoot = 'C:\serena_workspaces_directory\fetcher-simulator',
    [string] $BuildDirectory = 'MSVC2022_64',
    [int] $Port = 25567,
    [switch] $LiveProbe,
    [switch] $MidiProbe,
    [switch] $DawProbe,
    [switch] $DrumProbe,
    [switch] $GuitarProbe,
    [switch] $TimingProbe,
    [switch] $MixerProbe,
    [switch] $ReturnsProbe,
    [switch] $SoundSetProbe,
    [switch] $VanillaSynth,
    [switch] $ResetStage,
    [string] $BardcraftDataDirectory
)
$ErrorActionPreference = 'Stop'
if ($MidiProbe -or $DawProbe -or $DrumProbe -or $GuitarProbe -or $TimingProbe -or $MixerProbe -or $ReturnsProbe -or $SoundSetProbe) { $LiveProbe = $true }
function Write-Utf8 {
    param([string] $Path, [object] $Value)
    [System.IO.File]::WriteAllText($Path, ([string[]]$Value -join "`n"), [System.Text.UTF8Encoding]::new($false))
}
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $SimulatorRoot 'test-server\procedural-synth'
$runtime = Join-Path $repo "$BuildDirectory\RelWithDebInfo"
$assetSource = Join-Path $repo 'files\server\optional-assets\fetcher.procedural-synth'
$assetInstall = Join-Path $fixture 'synth-assets'
$assetModel = $null
if (-not $VanillaSynth -and (Test-Path -LiteralPath "$assetSource\asset-manifest.json")) {
    $assetManifest = Get-Content -LiteralPath "$assetSource\asset-manifest.json" -Raw | ConvertFrom-Json
    if ($assetManifest.version -ne 1 -or $assetManifest.model -notin @('meshes/fetcher/synth/keyboard.nif','meshes/fetcher/synth/keyboard-stand.nif')) {
        throw 'Unsupported synth assets manifest.'
    }
    $assetBase = [IO.Path]::GetFullPath($assetSource) + [IO.Path]::DirectorySeparatorChar
    foreach ($entry in $assetManifest.files) {
        if ([IO.Path]::IsPathRooted($entry.path) -or ($entry.path -split '[/\\]') -contains '..') {
            throw 'Invalid synth asset manifest path.'
        }
        $sourcePath = [IO.Path]::GetFullPath((Join-Path $assetSource $entry.path))
        if (-not $sourcePath.StartsWith($assetBase,[StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $sourcePath -PathType Leaf) -or
            (Get-Item -LiteralPath $sourcePath).Length -ne $entry.bytes -or
            (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Synth asset verification failed: $($entry.path)"
        }
    }
    $assetModel = $assetManifest.model
}
$runningFixture = Get-CimInstance Win32_Process | Where-Object {
    $_.ExecutablePath -eq "$fixture\openmw-test.exe" -or $_.ExecutablePath -eq "$fixture\openmw-server-test.exe"
}
if ($runningFixture) { throw 'Stop the isolated synth server, clients and crash monitors before running setup.' }
foreach ($name in @('openmw.exe','openmw-server.exe')) {
    if (-not (Test-Path -LiteralPath (Join-Path $runtime $name))) { throw "Build $name first." }
}
foreach ($dir in @($fixture,"$fixture\server-scripts","$fixture\server-lua-packages","$fixture\logs","$fixture\mp-keys","$fixture\userdata")) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}
if ($assetModel) {
    foreach ($entry in $assetManifest.files) {
        $destination = [IO.Path]::GetFullPath((Join-Path $assetInstall $entry.path))
        $installBase = [IO.Path]::GetFullPath($assetInstall) + [IO.Path]::DirectorySeparatorChar
        if (-not $destination.StartsWith($installBase,[StringComparison]::OrdinalIgnoreCase)) {
            throw 'Installed asset path escaped the fixture.'
        }
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath (Join-Path $assetSource $entry.path) -Destination $destination -Force
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash -ne $entry.sha256) {
            throw "Installed synth asset hash mismatch: $($entry.path)"
        }
    }
    Copy-Item -LiteralPath "$assetSource\asset-manifest.json" -Destination "$assetInstall\asset-manifest.json" -Force
}
Copy-Item -LiteralPath "$runtime\openmw.exe" -Destination "$fixture\openmw-test.exe" -Force
Copy-Item -LiteralPath "$runtime\openmw-server.exe" -Destination "$fixture\openmw-server-test.exe" -Force
Copy-Item -LiteralPath "$runtime\defaults.bin" -Destination "$fixture\defaults.bin" -Force
Get-ChildItem -LiteralPath $SimulatorRoot -Filter '*.dll' -File | Copy-Item -Destination $fixture -Force
foreach ($directory in @('osgPlugins-3.6.5')) {
    if (-not (Test-Path -LiteralPath "$fixture\$directory")) {
        New-Item -ItemType Junction -Path "$fixture\$directory" -Target "$SimulatorRoot\$directory" | Out-Null
    }
}
$scripts = Join-Path $repo 'apps\openmw-server\scripts'
Get-ChildItem -LiteralPath $scripts -File | Where-Object Extension -In '.lua','.omwscripts' | Copy-Item -Destination "$fixture\server-scripts" -Force
# Keep this local fixture focused on the synthesizer and standard server policy.
Write-Utf8 -Path "$fixture\server-scripts\server.omwscripts" -Value "GLOBAL: core.lua`nGLOBAL: procedural_synth.lua"
$config = Get-Content -LiteralPath "$scripts\config.lua" -Raw
$testZ = if ($assetModel -eq 'meshes/fetcher/synth/keyboard-stand.nif') { -703 } else { -650 }
$localSettings = @"
Config.PROCEDURAL_SYNTH_ENABLED = true
Config.PROCEDURAL_SYNTH_TEST_CELL = 'ToddTest'
Config.PROCEDURAL_SYNTH_TEST_POSITION = {x=2175,y=2256,z=$testZ}
Config.PROCEDURAL_SYNTH_TEST_MP_NUM = 1
Config.SPAWN_CELL = 'ToddTest'
Config.DEFAULT_SPAWN = {cell='ToddTest',position={x=2175,y=2176,z=-700}}
Config.DEFAULT_PLAYER_MARKS = {}
Config.ADMIN_HTTP_ENABLED = false
return Config
"@
if ($assetModel) { $localSettings = "Config.PROCEDURAL_SYNTH_MODEL = '$assetModel'`n" + $localSettings }
Write-Utf8 -Path "$fixture\server-scripts\config.lua" -Value ($config.Replace('return Config',$localSettings))
Copy-Item -LiteralPath "$repo\files\server\server-lua-packages\fetcher.procedural-synth" -Destination "$fixture\server-lua-packages" -Recurse -Force
$probe = Join-Path $fixture 'server-lua-packages\fetcher.synth-live-test'
if ($LiveProbe) {
    New-Item -ItemType Directory -Force -Path $probe | Out-Null
    $probeFile = if ($SoundSetProbe) { 'soundsets-player.lua' } elseif ($ReturnsProbe) { 'returns-player.lua' } elseif ($MixerProbe) { 'mixer-player.lua' } elseif ($TimingProbe) { 'timing-player.lua' } elseif ($GuitarProbe) { 'guitar-player.lua' } elseif ($DrumProbe) { 'drum-player.lua' } elseif ($DawProbe) { 'daw-player.lua' } elseif ($MidiProbe) { 'midi-player.lua' } else { 'live-player.lua' }
    Copy-Item -LiteralPath "$PSScriptRoot\procedural-synth-tests\$probeFile" -Destination "$probe\player.lua" -Force
    Write-Utf8 -Path "$probe\manifest.yaml" -Value @'
manifestVersion: 3
packageId: fetcher.synth-live-test
packageVersion: 1
requiredOpenMWLuaApi: 141
requiredMultiplayerLuaApi: 12
dependencies: [fetcher.procedural-synth]
files: [player.lua]
scripts:
  - path: player.lua
    flags: [player]
'@
} elseif (Test-Path -LiteralPath $probe) {
    # Move only this fixture's optional probe out of the package scan directory.
    $resolvedProbe = [System.IO.Path]::GetFullPath($probe)
    $resolvedFixture = [System.IO.Path]::GetFullPath($fixture) + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedProbe.StartsWith($resolvedFixture,[System.StringComparison]::OrdinalIgnoreCase)) {
        throw 'Probe path escaped the isolated fixture.'
    }
    Move-Item -LiteralPath $resolvedProbe -Destination (Join-Path $fixture ('disabled-probe-' + [DateTime]::UtcNow.Ticks))
}
$data = 'C:\GOG Games\Morrowind\Data Files'
if (-not (Test-Path -LiteralPath "$data\Morrowind.esm")) { throw 'Set the vanilla Morrowind data path in this script.' }
$resource = "$runtime\resources".Replace('\','/')
$vanilla = $data.Replace('\','/')
# Preserve the launcher's established fallback settings, with a vanilla-only content list.
$fallbacks = Get-Content -LiteralPath "$SimulatorRoot\openmw.cfg" | Where-Object { $_ -match '^fallback=' }
$openmw = @"
replace=config
resources="$resource"
data="$resource/vfs-mw"
data="$vanilla"
fallback-archive=Morrowind.bsa
fallback-archive=Tribunal.bsa
fallback-archive=Bloodmoon.bsa
content=Morrowind.esm
content=Tribunal.esm
content=Bloodmoon.esm
encoding=win1252
no-sound=0
user-data="$($fixture.Replace('\','/'))/userdata"
"@
if ($assetModel) { $openmw += "`ndata=`"$($assetInstall.Replace('\','/'))`"" }
if (-not $BardcraftDataDirectory) {
    $BardcraftDataDirectory = Join-Path $SimulatorRoot 'Data Files\fetcher-simulator\Gameplay\BardcraftOpenMW'
}
$midiSource = Join-Path $BardcraftDataDirectory 'midi'
if (Test-Path -LiteralPath $midiSource -PathType Container) {
    $midiInstall = Join-Path $fixture 'midi-data'
    $sourceBase = [IO.Path]::GetFullPath($midiSource) + [IO.Path]::DirectorySeparatorChar
    $midiFiles = @(Get-ChildItem -LiteralPath $midiSource -Recurse -File | Where-Object { $_.Extension -in '.mid','.midi' -and $_.Length -le 524288 } | Select-Object -First 512)
    foreach ($file in $midiFiles) {
        if (-not $file.FullName.StartsWith($sourceBase,[StringComparison]::OrdinalIgnoreCase)) { throw 'MIDI source path escaped its root.' }
        $relative = $file.FullName.Substring($sourceBase.Length)
        $destination = Join-Path "$midiInstall\midi" $relative
        New-Item -ItemType Directory -Force -Path (Split-Path -Parent $destination) | Out-Null
        Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
    }
    $openmw += "`ndata=`"$($midiInstall.Replace('\','/'))`""
    Write-Output "Installed $($midiFiles.Count) local Bardcraft MIDI files; original mod files preserved."
}
Write-Utf8 -Path "$fixture\openmw.cfg" -Value (@($openmw)+$fallbacks)
$server = @"
[server]
port=$Port
max_players=8
db_path=$($fixture.Replace('\','/'))/playerdata.db
[content]
openmw_cfg=$($fixture.Replace('\','/'))/openmw.cfg
resources=$resource
encoding=win1252
verify_determinism=true
[client_lua]
packages=$($fixture.Replace('\','/'))/server-lua-packages
[master]
public_listing=false
server_name=Local Procedural Synth
"@
Write-Utf8 -Path "$fixture\server.cfg" -Value $server
Write-Utf8 -Path "$fixture\userdata\settings.cfg" -Value @'
[Video]
resolution x = 1024
resolution y = 768
fullscreen = false
[Sound]
master volume = 0.8
music volume = 0
[Game]
show owned = 0
'@
# Copy the current client launcher workflow, with isolated names, binaries and profiles.
$launcher = Get-Content -LiteralPath "$SimulatorRoot\launch-test-clients.ps1" -Raw
$launcher = $launcher.Replace('127.0.0.1:25565',"127.0.0.1:$Port").Replace('Mario','Synth A').Replace('Sith Soldier','Synth B')
Write-Utf8 -Path "$fixture\launch-test-clients.ps1" -Value $launcher
Set-Content -LiteralPath "$fixture\launch-test-server.bat" -Value "@echo off`r`ncd /d `"%~dp0`"`r`nopenmw-server-test.exe --config server.cfg --log-dir logs`r`npause" -Encoding ascii
Set-Content -LiteralPath "$fixture\launch-test-clients.bat" -Value '@powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0launch-test-clients.ps1"' -Encoding ascii
if (-not (Test-Path -LiteralPath "$fixture\mp-keys\127.0.0.1_$Port.key")) {
    Copy-Item -LiteralPath "$SimulatorRoot\mp-keys\127.0.0.1_25565.key" -Destination "$fixture\mp-keys\127.0.0.1_$Port.key"
}
& python "$PSScriptRoot\seed-procedural-synth-test.py" "$SimulatorRoot\test-server\playerdata.db" "$fixture\playerdata.db"
if ($LASTEXITCODE -ne 0) { throw 'Failed to seed isolated characters.' }
if ($ResetStage) {
    & python "$PSScriptRoot\position-procedural-synth-stage.py" "$fixture\playerdata.db"
    if ($LASTEXITCODE -ne 0) { throw 'Failed to reset the isolated stage layout.' }
}
Write-Output "Synth fixture: $fixture"
if ($assetModel) { Write-Output "Keyboard assets installed and SHA-256 verified: $assetModel" }
else { Write-Output 'Using the vanilla lute fallback.' }
Write-Output 'Launch launch-test-server.bat, then launch-test-clients.bat. Synth A and Synth B start in ToddTest.'
