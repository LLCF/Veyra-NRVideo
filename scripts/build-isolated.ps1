[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$DependencyCache,
    [Parameter(Mandatory)][string]$TempDirectory,
    [string[]]$Targets=@('veyra'),
    [switch]$ConfigureOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
# Reuse only explicit dependency locations, never another tree's build state.
$cache=Get-Content -LiteralPath $DependencyCache
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(!$vs){throw 'MSVC not found'}
$vcvars=Join-Path $vs 'VC/Auxiliary/Build/vcvars64.bat'
$savedTemp=$env:TEMP;$savedTmp=$env:TMP
New-Item -ItemType Directory -Force -Path $BuildDirectory,$TempDirectory | Out-Null
try {
    $env:TEMP=$TempDirectory;$env:TMP=$TempDirectory
    $vcEnvironment=& cmd.exe /d /c "call `"$vcvars`" >nul && set"
    if($LASTEXITCODE){throw 'vcvars failed'}
    foreach($line in $vcEnvironment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
    $cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    $argsList=@('-S',$Root,'-B',$BuildDirectory,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_MAKE_PROGRAM=$ninja",'-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON','-DVEYRA_ENABLE_REMOTEPLAY=ON')
    foreach($line in $cache){
        if($line -match '^(VEYRA_[A-Z0-9_]+_ROOT|VEYRA_RP_CHIAKI_SOURCE_DIR|VEYRA_RP_CHIAKI_VERIFY_DIR|CMAKE_PREFIX_PATH|PROTOC|Protobuf_PROTOC_EXECUTABLE|PKG_CONFIG_EXECUTABLE):[^=]+=(.+)$'){
            $argsList+="-D$($matches[1])=$($matches[2])"
        }
    }
    $dlss=($cache | Select-String '^VEYRA_DLSS_SDK_ROOT:PATH=(.+)$').Matches[0].Groups[1].Value
    $nvidia=Split-Path $dlss -Parent
    $argsList+="-DVEYRA_NVOF_SDK_ROOT=$nvidia/Optical_Flow_SDK_5.0.7"
    & $cmake @argsList
    if($LASTEXITCODE){throw "Configure failed: $LASTEXITCODE"}
    if(!$ConfigureOnly){
        & $cmake --build $BuildDirectory --parallel 8 --target @Targets
        if($LASTEXITCODE){throw "Build failed: $LASTEXITCODE"}
    }
} finally {$env:TEMP=$savedTemp;$env:TMP=$savedTmp}
