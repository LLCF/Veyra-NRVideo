[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Build,
    [Parameter(Mandatory)][string]$Name,
    [string]$Output='E:/项目/Veyra/tests/beta-latency-ab-20260918',
    [int]$Seconds=120,
    [switch]$LegacyPhase
)
$ErrorActionPreference='Stop'
if($Seconds -lt 3 -or $Seconds -gt 120){throw 'Seconds must be 3..120'}
$dir=Join-Path $Output $Name
New-Item -ItemType Directory -Force $dir | Out-Null
$env:TEMP='E:/项目/Veyra/tmp/beta-latency-ab-20260918'
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
$env:VEYRA_TEST_LEGACY_CAPTURE_PHASE=if($LegacyPhase){'1'}else{$null}
$exe=Join-Path $Build 'veyra_capture_latency_tests.exe'
[ordered]@{build=$Build;name=$Name;seconds=$Seconds;legacyPhase=[bool]$LegacyPhase;executableSha256=(Get-FileHash -LiteralPath $exe).Hash} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'run.json') -Encoding utf8
$test=Start-Process -FilePath $exe -ArgumentList @(('"' + $dir + '"'),$Seconds) -WorkingDirectory $Build -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir 'result.txt') -RedirectStandardError (Join-Path $dir 'stderr.txt')
$handle=$test.Handle
if(!$test.WaitForExit(200000)){$test.Kill();throw 'Historical capture comparison exceeded 200 seconds'}
Get-Content -LiteralPath (Join-Path $dir 'result.txt')
if($test.ExitCode -ne 0){throw "Comparison failed: $($test.ExitCode)"}
