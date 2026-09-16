# Real-card acceptance for the capture ingress. Regression guard for the
# 1.3.2beta native-path bug: the compressed path and the uncompressed path are
# separate code paths, so BOTH must be exercised after any capture change.
#
# It enumerates the device's formats, picks the first uncompressed and the
# first compressed entry, then runs a real smoke capture on each. A regression
# like "every callback raises callbackError" shows up as a two-frames-per-epoch
# reconnect loop: frame counts collapse and dropped stays 0, so the frame-count
# floor below is the decisive check.
param(
  [Parameter(Mandatory = $true)][string]$Root,
  [string]$BuildDirectory,
  [int]$CaseSeconds = 12,
  [int]$Device = 0,
  [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $Root).Path
$bin = Join-Path $root 'out/build/x64-release'
if ($BuildDirectory) { $bin = (Resolve-Path -LiteralPath $BuildDirectory).Path }
$player = Join-Path $bin 'veyra.exe'
$lister = Join-Path $bin 'veyra_capture_tests.exe'
if (-not (Test-Path -LiteralPath $player)) { throw "missing $player" }
if (-not (Test-Path -LiteralPath $lister)) { throw "missing $lister" }
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $root ('logs/capture-paths/' + [Guid]::NewGuid().ToString('N')) }
[IO.Directory]::CreateDirectory($out) | Out-Null

$listing = & $lister --list 2>$null
$native = $null
$compressed = $null
foreach ($line in $listing) {
  if ($line -notmatch '^FORMAT\s+\d+:(\d+)\s+(.+)$') { continue }
  $index = [int]$Matches[1]
  $label = $Matches[2]
  if (-not $native -and $label -match 'YUY2|NV12|UYVY|YVYU|RGB') { $native = $index }
  if (-not $compressed -and $label -match 'MJPEG|MJPG|H264|H\.264|HEVC|H265') { $compressed = $index }
}
if ($null -eq $native -and $null -eq $compressed) { throw 'no capture formats enumerated; is the device free?' }

$results = [Collections.Generic.List[object]]::new()
$failures = 0
$cases = @()
if ($null -ne $native) { $cases += [pscustomobject]@{ name = 'uncompressed'; index = $native } }
if ($null -ne $compressed) { $cases += [pscustomobject]@{ name = 'compressed'; index = $compressed } }

foreach ($case in $cases) {
  $logPath = Join-Path $out ("$($case.name).stdout.log")
  $argument = 'capture:' + $Device + ':' + $case.index + ':-1:0'
  # Start-Process does not reliably populate ExitCode on Windows PowerShell
  # once redirection is involved, so drive the child through .NET directly.
  $psi = New-Object System.Diagnostics.ProcessStartInfo
  $psi.FileName = $player
  $psi.Arguments = '"' + $argument + '" --smoke-seconds ' + $CaseSeconds + ' --no-nr --no-sr --no-fg'
  $psi.WorkingDirectory = $root
  $psi.UseShellExecute = $false
  $psi.RedirectStandardOutput = $true
  $psi.RedirectStandardError = $true
  $psi.CreateNoWindow = $true
  $proc = [System.Diagnostics.Process]::Start($psi)
  $stdoutTask = $proc.StandardOutput.ReadToEndAsync()
  $stderrTask = $proc.StandardError.ReadToEndAsync()
  if (-not $proc.WaitForExit(($CaseSeconds + 60) * 1000)) { $proc.Kill(); throw "$($case.name) timeout" }
  $exitCode = $proc.ExitCode
  $text = $stdoutTask.Result
  Set-Content -LiteralPath $logPath -Value $text -Encoding UTF8
  Set-Content -LiteralPath (Join-Path $out ("$($case.name).stderr.log")) -Value $stderrTask.Result -Encoding UTF8
  $ok = $false
  $detail = 'no smoke line'
  if ($text -match 'smoke frames=(\d+).*captureDropped=(\d+)') {
    $frames = [int]$Matches[1]
    $dropped = [int]$Matches[2]
    $floor = [Math]::Max(20, $CaseSeconds * 8) # slowest plausible real capture is 18 fps
    $ok = ($exitCode -eq 0) -and ($frames -ge $floor) -and ($dropped -eq 0) -and ($text -match 'failed=false')
    $detail = "exit=$exitCode frames=$frames dropped=$dropped floor=$floor"
  }
  if (-not $ok) { ++$failures }
  $results.Add([ordered]@{ case = $case.name; formatIndex = $case.index; passed = $ok; detail = $detail })
  Write-Host (('{0} {1} format={2} {3}' -f $(if ($ok) { 'PASS' } else { 'FAIL' }), $case.name, $case.index, $detail))
}

[ordered]@{ cases = $results; seconds = $CaseSeconds; failures = $failures } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $out 'result.json') -Encoding UTF8
if ($failures -ne 0) { Write-Host "CAPTURE PATHS FAIL ($out)"; exit 1 }
Write-Host "CAPTURE PATHS PASS ($out)"
exit 0
