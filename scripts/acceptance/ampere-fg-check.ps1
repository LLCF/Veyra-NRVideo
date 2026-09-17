# Turnkey RTX 30 frame-generation check for a tester: runs the packaged player
# with --fg, then extracts the decisive log lines into one report. Works for
# files and capture paths ("capture:0:24:-1:0").
param(
  [Parameter(Mandatory = $true)][string]$PackageDirectory,
  [Parameter(Mandatory = $true)][string]$Source,
  [int]$Seconds = 15,
  [int]$Multiplier = 0,          # 0 = leave the default (2X); 6 = try 6X
  [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path -LiteralPath $PackageDirectory).Path
$exe = Join-Path $package 'Veyra.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw "missing $exe" }
$logPath = Join-Path $package 'logs/veyra-app.log'
$out = if ($OutputDirectory) { [IO.Path]::GetFullPath($OutputDirectory) } else { Join-Path $package 'logs/ampere-fg-check' }
[IO.Directory]::CreateDirectory($out) | Out-Null

$startLength = if (Test-Path -LiteralPath $logPath) { (Get-Item -LiteralPath $logPath).Length } else { 0 }

$arguments = '"' + $Source + '" --fg --smoke-seconds ' + $Seconds
if ($Multiplier -gt 0) { $arguments += ' --fg-multiplier ' + $Multiplier }
Write-Host "running: $exe $arguments"
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $exe
$psi.Arguments = $arguments
$psi.WorkingDirectory = $package
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.CreateNoWindow = $true
$proc = [System.Diagnostics.Process]::Start($psi)
$stdoutTask = $proc.StandardOutput.ReadToEndAsync()
$stderrTask = $proc.StandardError.ReadToEndAsync()
$finished = $proc.WaitForExit(($Seconds + 180) * 1000)
$forcedStop = $false
if (-not $finished) { $forcedStop = $true; Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
$stdout = $stdoutTask.Result
$stderr = $stderrTask.Result
$exitCode = if ($forcedStop) { 'timeout' } else { $proc.ExitCode }
Set-Content -LiteralPath (Join-Path $out 'run.stdout.log') -Value $stdout -Encoding UTF8
Set-Content -LiteralPath (Join-Path $out 'run.stderr.log') -Value $stderr -Encoding UTF8

# Only look at the bytes this run appended: reading from the saved offset keeps
# a previous session from answering for this one.
$tail = @()
if (Test-Path -LiteralPath $logPath) {
  $stream = [IO.File]::Open($logPath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
  try {
    if ($startLength -le $stream.Length) {
      $stream.Seek($startLength, [IO.SeekOrigin]::Begin) | Out-Null
      $reader = New-Object IO.StreamReader($stream)
      $tail = ($reader.ReadToEnd() -split "`r?`n")
    }
  } finally { $stream.Dispose() }
}

function FirstMatch([string]$pattern) {
  foreach ($line in $tail) { if ($line -match $pattern) { return $Matches[0] } }
  return $null
}
$adapterLine = FirstMatch 'adapter\[0\][^\r\n]*'
$unlockLine = FirstMatch '\[ampere-mfg\][^\r\n]*spoofed=[^\r\n]*'
$spoofLine = FirstMatch '\[nvapi-spoof\][^\r\n]*'
$capabilityLine = FirstMatch '\[graph\] FG capability[^\r\n]*'
$createLine = FirstMatch 'Create DLSSG[^\r\n]*'
$evaluateFault = FirstMatch 'fg-backend failed[^\r\n]*'
$smokeLine = $null
foreach ($line in $tail) { if ($line -match '\[app\] smoke frames=[^\r\n]*') { $smokeLine = $Matches[0] } }

$fields = [ordered]@{
  package = $package
  source = $Source
  seconds = $Seconds
  multiplier = $Multiplier
  exit = $exitCode
  forcedStop = $forcedStop
  adapter = $adapterLine
  ampereUnlock = $unlockLine
  nvapiSpoof = $spoofLine
  fgCapability = $capabilityLine
  createFeature = $createLine
  evaluateFault = $evaluateFault
  smoke = $smokeLine
}
$report = Join-Path $out 'report.json'
$fields | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $report -Encoding UTF8

Write-Host ''
Write-Host '=== RTX 30 frame-generation check ==='
foreach ($key in $fields.Keys) { Write-Host ("{0,-14}: {1}" -f $key, $fields[$key]) }
Write-Host ''
if (-not $capabilityLine -or $capabilityLine -notmatch 'available=true') {
  Write-Host 'RESULT: frame generation did not become available - send this report back.'
  exit 1
}
if ($evaluateFault) {
  Write-Host 'RESULT: capability passed but evaluation failed - send this report back.'
  exit 1
}
if (-not $smokeLine -or $smokeLine -notmatch 'generated=(\d+)' -or [int]$Matches[1] -eq 0) {
  Write-Host 'RESULT: no generated frames observed - send this report back.'
  exit 1
}
Write-Host 'RESULT: frame generation is running on this machine.'
exit 0
