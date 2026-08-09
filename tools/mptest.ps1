# Two-process multiplayer smoke test: one host, one joining client, standing
# 100 units apart facing each other. Each writes a screenshot so you can see
# the other player's body, name tag and health bar replicated over UDP.
param([string]$Root = (Split-Path -Parent $PSScriptRoot))

$exe = Join-Path $Root "bin\kaj_shooter.exe"
New-Item -ItemType Directory -Force -Path (Join-Path $Root "testshots") | Out-Null
Set-Location $Root

Get-Process kaj_shooter -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 400

$hostArgs = @("--host", "--bots", "0", "--name", "HostKaj", "--width", "1024",
              "--height", "640", "--autopos", "1000", "4", "1200",
              "--autoyaw", "180", "--autopitch", "0", "--autopin",
              "--autoshot", "11", "testshots/mp_host.png")
$hostProc = Start-Process -FilePath $exe -ArgumentList $hostArgs -PassThru `
    -RedirectStandardOutput (Join-Path $Root "testshots\mp_host.log") `
    -RedirectStandardError  (Join-Path $Root "testshots\mp_host.err")

Start-Sleep -Seconds 3

$cliArgs = @("--connect", "127.0.0.1", "--name", "JoinerBob", "--width", "1024",
             "--height", "640", "--autopos", "880", "4", "1200",
             "--autoyaw", "0", "--autopitch", "0", "--autopin",
             "--autoshot", "8", "testshots/mp_client.png")
$cliProc = Start-Process -FilePath $exe -ArgumentList $cliArgs -PassThru `
    -RedirectStandardOutput (Join-Path $Root "testshots\mp_client.log") `
    -RedirectStandardError  (Join-Path $Root "testshots\mp_client.err")

$cliProc.WaitForExit(60000) | Out-Null
$hostProc.WaitForExit(60000) | Out-Null
Get-Process kaj_shooter -ErrorAction SilentlyContinue | Stop-Process -Force

Write-Host "--- host log ---"
Get-Content (Join-Path $Root "testshots\mp_host.log") -ErrorAction SilentlyContinue |
    Select-String -Pattern "DEBUG:|SERVER:|CLIENT:" | ForEach-Object { Write-Host "  $_" }
Write-Host "--- client log ---"
Get-Content (Join-Path $Root "testshots\mp_client.log") -ErrorAction SilentlyContinue |
    Select-String -Pattern "DEBUG:|SERVER:|CLIENT:" | ForEach-Object { Write-Host "  $_" }
