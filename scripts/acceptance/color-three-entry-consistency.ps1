[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Root,[string]$BuildDirectory,[string]$PlayerExe)
# Colour page T6: preview/screenshot and export must agree.
#
# The preview entry point is the presented frame (the GPU contract test already
# pins its pixels); the screenshot smoke saves that same sink output, and the
# export job renders the identical EnhanceGraph in a worker process. This script
# therefore compares the screenshot PNG against a frame decoded out of the
# exported video, with the colour grade both off and on:
#   * grade on  -> screenshot vs export frame must match closely (encoder loss only)
#   * grade off vs on -> both entries must show the same, clearly visible change
# A static source pattern removes frame-alignment questions; run with ffmpeg on PATH.
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
Set-Location -LiteralPath $Root
$run=[Guid]::NewGuid().ToString('N')
$dir=Join-Path $Root ('logs/color/three-entry/'+$run)
[IO.Directory]::CreateDirectory($dir)|Out-Null
$bin=Join-Path $Root 'out/build/x64-release'
if($BuildDirectory){$bin=(Resolve-Path -LiteralPath $BuildDirectory).Path}
if(-not $PlayerExe){$PlayerExe=Join-Path $bin 'veyra.exe'}
$PlayerExe=(Resolve-Path -LiteralPath $PlayerExe).Path
$ffmpeg=(Get-Command ffmpeg -ErrorAction Stop).Source
$checks=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[bool]$Passed,[string]$Detail){
    $checks.Add([pscustomobject]@{name=$Name;passed=$Passed;detail=$Detail})
    Write-Host ("{0} {1} :: {2}" -f ($(if($Passed){'PASS'}else{'FAIL'})),$Name,$Detail)
    if(-not $Passed){throw "$Name : $Detail"}
}
function Run([string]$Name,[string[]]$Argv,[int]$Limit=30){
    $startArgs=@{FilePath=$PlayerExe;WorkingDirectory=$Root;PassThru=$true;WindowStyle='Hidden';
        RedirectStandardOutput="$dir/$Name.stdout.log";RedirectStandardError="$dir/$Name.stderr.log"}
    $forwarded=@($Argv|ForEach-Object{'"'+$_.Replace('"','\"')+'"'})
    if($forwarded.Count -gt 0){$startArgs.ArgumentList=$forwarded}
    $p=Start-Process @startArgs
    # Keep the native handle alive: without it PowerShell can hand back a null
    # ExitCode for a GUI-subsystem process.
    $processHandle=$p.Handle
    if(-not $p.WaitForExit($Limit*1000)){Stop-Process -Id $p.Id -Force;throw "$Name timeout"}
    Check "$Name-exit" ($p.ExitCode -eq 0) "exit=$($p.ExitCode)"
}
function PsnrDb([string]$Reference,[string]$Test){
    # ffmpeg writes PSNR to stderr (which PowerShell 7 turns into a terminating
    # error under -Stop), so drive the filter through its stats file instead.
    $previous=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    try{ $output=(& $ffmpeg -hide_banner -loglevel info -i $Reference -i $Test -lavfi psnr -f null - 2>&1 | Out-String) } finally { $ErrorActionPreference=$previous }
    $match=[regex]::Match($output,'average:([0-9.]+|inf)')
    if(-not $match.Success){throw "psnr parse failed for $Reference vs $Test"}
    if($match.Groups[1].Value -eq 'inf'){return 99.0}
    return [double]$match.Groups[1].Value
}
try{
    $clip=Join-Path $dir 'static-pattern.mp4'
    & $ffmpeg -hide_banner -loglevel error -y -f lavfi -i 'color=c=0x303030:s=1280x720:r=30' `
        -vf 'drawbox=x=320:y=180:w=320:h=360:color=0xC0C0C0:t=fill,drawbox=x=800:y=180:w=320:h=360:color=0x804020:t=fill' `
        -t 4 -c:v libx264 -pix_fmt yuv420p -crf 12 $clip
    if($LASTEXITCODE -ne 0){throw 'pattern render failed'}
    Run shot-off @($clip,'--smoke-seconds','8','--smoke-controls','--smoke-save',"$dir/shot-off.png")
    Run shot-on  @($clip,'--smoke-seconds','8','--smoke-controls','--smoke-save',"$dir/shot-on.png",'--color-grade=1.0')
    Run export-off @($clip,'--export-out',"$dir/export-off.mp4",'--max-frames','4')
    Run export-on  @($clip,'--export-out',"$dir/export-on.mp4",'--max-frames','4','--color-grade=1.0')
    foreach($entry in @('export-off','export-on')){
        & $ffmpeg -hide_banner -loglevel error -y -i "$dir/$entry.mp4" -vf "select='eq(n,1)'" -frames:v 1 "$dir/$entry-frame.png"
        if($LASTEXITCODE -ne 0){throw "$entry frame extraction failed"}
    }
    $consistent=PsnrDb "$dir/shot-on.png" "$dir/export-on-frame.png"
    $changedShot=PsnrDb "$dir/shot-off.png" "$dir/shot-on.png"
    $changedExport=PsnrDb "$dir/export-off-frame.png" "$dir/export-on-frame.png"
    $consistentOff=PsnrDb "$dir/shot-off.png" "$dir/export-off-frame.png"
    # Thresholds: measured values are recorded in result.json. 36 dB is a loose
    # bound for NVENC quantisation on this pattern; 30 dB proves the grade is
    # visible in both entries instead of being applied in only one of them.
    Check screenshot-vs-export ($consistent -ge 36) ("psnr={0:N2}dB (grade on)" -f $consistent)
    Check screenshot-vs-export-baseline ($consistentOff -ge 36) ("psnr={0:N2}dB (grade off)" -f $consistentOff)
    Check grade-visible-in-both ($changedShot -lt 30 -and $changedExport -lt 30) ("screenshot {0:N2}dB / export {1:N2}dB between off and on" -f $changedShot,$changedExport)
    $result=[pscustomobject]@{
        pass=$true;runId=$run;
        psnrScreenshotVsExportGradeOn=[math]::Round($consistent,3);
        psnrScreenshotVsExportGradeOff=[math]::Round($consistentOff,3);
        psnrGradeChangeScreenshot=[math]::Round($changedShot,3);
        psnrGradeChangeExport=[math]::Round($changedExport,3);
        checks=$checks}
    $result|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 (Join-Path $dir 'result.json')
    Write-Host ('DELIVERY COLOUR THREE-ENTRY PASS -> '+(Join-Path $dir 'result.json'))
}catch{
    [pscustomobject]@{pass=$false;runId=$run;error=$_.Exception.Message;checks=$checks}|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 (Join-Path $dir 'result.json')
    Write-Host ('DELIVERY COLOUR THREE-ENTRY FAIL -> '+$_.Exception.Message)
    exit 1
}
