param([string]$Fixture = $PSScriptRoot, [switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
$serverConfig = Join-Path $Fixture 'server.cfg'
$match = Select-String -LiteralPath $serverConfig -Pattern '^\s*port\s*=\s*(\d+)\s*$' | Select-Object -First 1
if (-not $match) { throw 'Cannot find the test server port in server.cfg.' }
$testPort = [int]$match.Matches[0].Groups[1].Value
$serverLock = New-Object System.Threading.Mutex($false, "Local\FetcherStudioTestServer-$testPort")
$locked = $false
try {
    try { $locked = $serverLock.WaitOne(0) } catch [System.Threading.AbandonedMutexException] { $locked = $true }
    $existing = Get-NetUDPEndpoint -ErrorAction Stop | Where-Object LocalPort -eq $testPort
    if (-not $locked -or $existing) {
        Write-Host "A server is already using UDP port $testPort. Use the running server; no second server was started."
        exit 1
    }
    if ($CheckOnly) { Write-Output "UDP port $testPort is available."; exit 0 }
    Push-Location -LiteralPath $Fixture
    try { & (Join-Path $Fixture 'openmw-server-test.exe') --config server.cfg --log-dir logs; exit $LASTEXITCODE }
    finally { Pop-Location }
} finally {
    if ($locked) { $serverLock.ReleaseMutex() }
    $serverLock.Dispose()
}
