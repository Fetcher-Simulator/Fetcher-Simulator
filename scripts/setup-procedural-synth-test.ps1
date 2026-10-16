[CmdletBinding()]
param(
    [string] $SimulatorRoot = 'C:\serena_workspaces_directory\fetcher-simulator',
    [string] $BuildDirectory = 'MSVC2022_64',
    [int] $Port = 25567,
    [switch] $LiveProbe
)
$ErrorActionPreference = 'Stop'
function Write-Utf8 {
    param([string] $Path, [object] $Value)
    [System.IO.File]::WriteAllText($Path, ([string[]]$Value -join "`n"), [System.Text.UTF8Encoding]::new($false))
}
$repo = Split-Path -Parent $PSScriptRoot
$fixture = Join-Path $SimulatorRoot 'test-server\procedural-synth'
$runtime = Join-Path $repo "$BuildDirectory\RelWithDebInfo"
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
$localSettings = @"
Config.PROCEDURAL_SYNTH_ENABLED = true
Config.PROCEDURAL_SYNTH_TEST_CELL = 'ToddTest'
Config.PROCEDURAL_SYNTH_TEST_POSITION = {x=2175,y=2256,z=-650}
Config.PROCEDURAL_SYNTH_TEST_MP_NUM = 1
Config.SPAWN_CELL = 'ToddTest'
Config.DEFAULT_SPAWN = {cell='ToddTest',position={x=2175,y=2176,z=-700}}
Config.DEFAULT_PLAYER_MARKS = {}
Config.ADMIN_HTTP_ENABLED = false
return Config
"@
Write-Utf8 -Path "$fixture\server-scripts\config.lua" -Value ($config.Replace('return Config',$localSettings))
Copy-Item -LiteralPath "$repo\files\server\server-lua-packages\fetcher.procedural-synth" -Destination "$fixture\server-lua-packages" -Recurse -Force
$probe = Join-Path $fixture 'server-lua-packages\fetcher.synth-live-test'
if ($LiveProbe) {
    New-Item -ItemType Directory -Force -Path $probe | Out-Null
    Copy-Item -LiteralPath "$PSScriptRoot\procedural-synth-tests\live-player.lua" -Destination "$probe\player.lua" -Force
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
Write-Output "Synth fixture: $fixture"
Write-Output 'Launch launch-test-server.bat, then launch-test-clients.bat. Synth A and Synth B start in ToddTest.'
