[CmdletBinding()]
param(
    [string]$Build='E:/项目/Veyra/build/frame-pacing-20260918',
    [string]$Media='E:/项目/Veyra/tests/1.4.2beta/visible-scene.mkv',
    [string]$Output='E:/项目/Veyra/tests/frame-pacing-20260918',
    [string]$PresentMon='E:/项目/Veyra/downloads/PresentMon-2.5.1-x64.exe',
    [string]$Name='off', [int]$Mode=-1,[int]$Sync=0,[int]$Multiplier=1,[int]$Seconds=10,[string]$Scenario='normal',
    [switch]$CaptureDisplay
)
$ErrorActionPreference='Stop'
$dir=Join-Path $Output $Name
New-Item -ItemType Directory -Force $dir | Out-Null
$env:TEMP='E:/项目/Veyra/tmp/frame-pacing-20260918';$env:TMP=$env:TEMP
$monitorArgs=@('--process_name','veyra_presentation_pacing_tests.exe','--output_file',('"'+(Join-Path $dir 'presentmon.csv')+'"'),'--no_console_stats','--qpc_time','--timed',($Seconds+35),'--terminate_after_timed','--session_name',('VeyraPacing'+$PID))
$monitor=$null
if($CaptureDisplay){$monitor=Start-Process -FilePath $PresentMon -ArgumentList $monitorArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir 'presentmon.stdout') -RedirectStandardError (Join-Path $dir 'presentmon.stderr')}
try {
    Start-Sleep -Milliseconds 700
    $testArgs=@($Media,$dir,$Mode,$Sync,$Multiplier,$Seconds,$Scenario)|ForEach-Object{'"'+$_+'"'}
    $test=Start-Process -FilePath (Join-Path $Build 'veyra_presentation_pacing_tests.exe') -ArgumentList $testArgs -WorkingDirectory $Build -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $dir 'result.txt') -RedirectStandardError (Join-Path $dir 'stderr.txt')
    $handle=$test.Handle
    if(!$test.WaitForExit(240000)){$test.Kill();throw 'Pacing acceptance exceeded 240 seconds'}
    $exit=$test.ExitCode
    # End only this test's ETW session; PresentMon flushes its pending events.
    if($monitor -and !$monitor.HasExited){
        & $PresentMon --session_name ('VeyraPacing'+$PID) --terminate_existing_session | Out-Null
        if(!$monitor.WaitForExit(10000)){$monitor.Kill();throw 'PresentMon did not finish'}
    }
    Get-Content (Join-Path $dir 'result.txt')
    if($CaptureDisplay){Get-Content (Join-Path $dir 'presentmon.stderr')}
    if($exit -ne 0){throw "Acceptance exit=$exit"}
} finally {if($monitor -and !$monitor.HasExited){$monitor.Kill()}}
