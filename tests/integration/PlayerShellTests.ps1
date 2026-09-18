param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$Media,
    [string]$Root=(Get-Location).Path
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path -LiteralPath $Root).Path
$BuildDirectory=(Resolve-Path -LiteralPath $BuildDirectory).Path
$Media=(Resolve-Path -LiteralPath $Media).Path
$sandbox=Join-Path $Root ('out/tests/player-shell-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path "$sandbox/runtime_local","$sandbox/logs" | Out-Null
Copy-Item -LiteralPath "$BuildDirectory/veyra.exe" -Destination $sandbox
Get-ChildItem -LiteralPath $BuildDirectory -Filter '*.dll' | Copy-Item -Destination $sandbox
foreach($component in 'nvidia','intel','amd'){
    New-Item -ItemType Junction -Path "$sandbox/runtime_local/$component" -Target "$Root/runtime_local/$component" | Out-Null
}
New-Item -ItemType Junction -Path "$sandbox/shaders" -Target "$BuildDirectory/shaders" | Out-Null
Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ShellTestNative {
 public delegate bool EnumProc(IntPtr window,IntPtr context);
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback,IntPtr context);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr window,StringBuilder name,int size);
 [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window,int id);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window,int command);
 [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr window,uint message,IntPtr wp,IntPtr lp);
 [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window,uint message,IntPtr wp,IntPtr lp);
 public static IntPtr Find(uint process,string cls){
  IntPtr result=IntPtr.Zero;
  EnumWindows((window,context)=>{uint p;GetWindowThreadProcessId(window,out p);var name=new StringBuilder(256);GetClassName(window,name,256);if(p==process&&name.ToString()==cls){result=window;return false;}return true;},IntPtr.Zero);
  return result;
 }
}
'@
function Wait-Condition([scriptblock]$Condition,[string]$Label){
    $deadline=[DateTime]::UtcNow.AddSeconds(25)
    do{if(& $Condition){return};Start-Sleep -Milliseconds 25}while([DateTime]::UtcNow -lt $deadline)
    throw "Timeout: $Label"
}
function Post($Window,$Message,$Wparam,$Lparam=0){[void][ShellTestNative]::PostMessageW($Window,$Message,[intptr]$Wparam,[intptr]$Lparam)}
function Send($Window,$Message,$Wparam=0,$Lparam=0){[ShellTestNative]::SendMessageW($Window,$Message,[intptr]$Wparam,[intptr]$Lparam).ToInt64()}
$script:app=$null
$oldLog=$env:VEYRA_LOG_FILE
function Start-Player([string]$Arguments,[string]$Label){
    $script:log=Join-Path $sandbox "logs/$Label.log"
    $env:VEYRA_LOG_FILE=$script:log
    if($Arguments){$script:app=Start-Process -FilePath "$sandbox/veyra.exe" -ArgumentList $Arguments -WindowStyle Hidden -PassThru}
    else{$script:app=Start-Process -FilePath "$sandbox/veyra.exe" -WindowStyle Hidden -PassThru}
    Wait-Condition { [ShellTestNative]::Find($script:app.Id,'VeyraApp') -ne [intptr]::Zero } 'main window'
    $script:window=[ShellTestNative]::Find($script:app.Id,'VeyraApp')
    # The popup contract requires a visible anchor; show only this test window.
    [void][ShellTestNative]::ShowWindow($script:window,5)
}
function Close-Player {
    Post $script:window 0x10 0
    if(!$script:app.WaitForExit(25000)){throw 'Normal close timed out'}
    if($script:app.ExitCode -ne 0){throw "Player exit $($script:app.ExitCode)"}
    $script:app=$null
}
function Log-Matches([string]$Pattern){(Test-Path -LiteralPath $script:log) -and (Select-String -LiteralPath $script:log -Pattern $Pattern -Quiet)}
function Select-Popup([int]$Row,[int]$Count=0){
    Wait-Condition { [ShellTestNative]::Find($script:app.Id,'VeyraGlassSelector') -ne [intptr]::Zero } 'selector'
    $popup=[ShellTestNative]::Find($script:app.Id,'VeyraGlassSelector')
    $list=[ShellTestNative]::GetDlgItem($popup,1)
    if($Count -and (Send $list 0x18B) -ne $Count){throw "Wrong popup count, expected $Count"}
    [void](Send $list 0x186 $Row)
    Post $list 0x100 13
    Wait-Condition { [ShellTestNative]::Find($script:app.Id,'VeyraGlassSelector') -ne $popup } 'selector accepted'
}
try{
    Start-Player '--nr-community' 'preferences-first'
    [void](Send $window 0x802C 200 1)
    [void](Send $window 0x802C 201 1)
    [void](Send $window 0x111 224)
    Close-Player
    $preset=Get-Content -LiteralPath "$sandbox/runtime_local/last-applied.v1" -Raw
    if($preset -notmatch '"Last applied" 1 1 1 '){throw 'NR/SR preferences missing'}
    Start-Player '' 'preferences-restart'
    if((Send ([ShellTestNative]::GetDlgItem($window,105)) 0xF0) -ne 1){throw 'NR not restored'}
    if((Send ([ShellTestNative]::GetDlgItem($window,106)) 0xF0) -ne 1){throw 'SR not restored'}
    Close-Player
    if((Get-Content -LiteralPath "$sandbox/runtime_local/last-applied.v1" -Raw) -ne $preset){throw 'Restart changed saved settings'}
    Start-Player '' 'preferences-bypass'
    [void](Send $window 0x111 221)
    Close-Player
    if((Get-Content -LiteralPath "$sandbox/runtime_local/last-applied.v1" -Raw) -ne $preset){throw 'Master bypass erased configured effects'}
    if((Get-Content -LiteralPath "$sandbox/runtime_local/ui-preferences.v1" -Raw).Trim() -notmatch ' 0$'){throw 'Master bypass not saved'}
    Write-Output 'PASS normal GUI close/restart preserves NR, SR, runtime choice and bypass draft'
    Start-Player ('"'+$Media+'"') 'playback'
    Wait-Condition {Log-Matches 'playback display/system request acquired'} 'playing power guard'
    [void](Send $window 0x111 120)
    [void](Send $window 0x112 0xF140)
    [void](Send $window 0x111 102)
    Wait-Condition {Log-Matches 'playback display/system request released'} 'pause power release'
    [void](Send $window 0x111 102)
    Wait-Condition {((Select-String -LiteralPath $log -Pattern 'playback display/system request acquired').Count -ge 2)} 'resume power guard'
    [void](Send $window 0x111 120)
    Wait-Condition {Log-Matches 'single-pass complete tracks=32'} 'subtitle scan'
    Post $window 0x111 225 ([ShellTestNative]::GetDlgItem($window,225))
    Select-Popup 1
    Select-Popup 32 33
    Wait-Condition {Log-Matches 'selected secondary=false track=31 stream=49 enabled=true'} 'last subtitle selected'
    Post $window 0x111 225 ([ShellTestNative]::GetDlgItem($window,225))
    Select-Popup 0
    Select-Popup 0 17
    Wait-Condition {Log-Matches 'audio-track.*requested=1 selected=1 changed=true'} 'first audio selected'
    Close-Player
    if((Select-String -LiteralPath $log -Pattern 'playback display/system request released').Count -lt 2){throw 'Close did not release power guard'}
    Write-Output 'PASS normal GUI 32nd subtitle and audio selection, fullscreen, pause/resume/close power lifecycle'
    Write-Output "Evidence: $sandbox"
}finally{
    $env:VEYRA_LOG_FILE=$oldLog
    if($script:app -and !$script:app.HasExited){Post $script:window 0x10 0;[void]$script:app.WaitForExit(25000)}
}
