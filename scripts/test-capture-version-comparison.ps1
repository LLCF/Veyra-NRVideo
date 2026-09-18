[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Build,
    [Parameter(Mandatory)][string]$Name,
    [string]$Output='E:/项目/Veyra/tests/beta-latency-ab-20260918',
    [int]$Seconds=120,
    [switch]$LegacyPhase,
    [ValidateSet('dlss','xess')][string]$Backend='dlss',
    [ValidateSet(2,4)][int]$Multiplier=4,
    [ValidateSet('realtime','native')][string]$Nr='realtime',
    [switch]$Resize,
    [string]$TempDirectory='E:/项目/Veyra/tmp/beta-latency-ab-20260918'
)
$ErrorActionPreference='Stop'
if($Seconds -lt 3 -or $Seconds -gt 120){throw 'Seconds must be 3..120'}
$dir=Join-Path $Output $Name
New-Item -ItemType Directory -Force $dir | Out-Null
$env:TEMP=$TempDirectory
$env:TMP=$env:TEMP
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
$env:VEYRA_TEST_LEGACY_CAPTURE_PHASE=if($LegacyPhase){'1'}else{$null}
$exe=Join-Path $Build 'veyra_capture_latency_tests.exe'
[ordered]@{build=$Build;name=$Name;seconds=$Seconds;backend=$Backend;multiplier=$Multiplier;nr=$Nr;resize=[bool]$Resize;legacyPhase=[bool]$LegacyPhase;executableSha256=(Get-FileHash -LiteralPath $exe).Hash} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $dir 'run.json') -Encoding utf8
$testArgs=@(('"' + $dir + '"'),$Seconds)
# Preserve compatibility with the immutable historical probe's two arguments.
if($Backend -ne 'dlss' -or $Multiplier -ne 4 -or $Nr -ne 'realtime' -or $Resize){$testArgs+=@($Backend,$Multiplier,$Nr,$(if($Resize){'resize'}else{'steady'}))}
$test=Start-Process -FilePath $exe -ArgumentList $testArgs -WorkingDirectory $Build -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir 'result.txt') -RedirectStandardError (Join-Path $dir 'stderr.txt')
$handle=$test.Handle
if(!$test.WaitForExit(200000)){$test.Kill();throw 'Historical capture comparison exceeded 200 seconds'}
Get-Content -LiteralPath (Join-Path $dir 'result.txt')
if($test.ExitCode -ne 0){throw "Comparison failed: $($test.ExitCode)"}
