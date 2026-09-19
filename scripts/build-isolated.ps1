[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Root,
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$DependencyCache,
    [Parameter(Mandatory)][string]$TempDirectory,
    [string[]]$Targets=@('veyra'),
    [string]$DisplayVersion,
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
$savedTemp=$env:TEMP;$savedTmp=$env:TMP;$savedVsLang=$env:VSLANG
$savedCodePage=(& cmd.exe /d /c chcp) -replace '[^0-9]',''
if(!$savedCodePage){throw 'Cannot read build console encoding'}
New-Item -ItemType Directory -Force -Path $BuildDirectory,$TempDirectory | Out-Null
try {
    $env:TEMP=$TempDirectory;$env:TMP=$TempDirectory
    # CMake and Ninja must decode the same /showIncludes bytes, including on
    # installations that have only the Chinese MSVC language pack.
    & cmd.exe /d /c 'chcp 65001 >nul'
    if($LASTEXITCODE){throw 'Cannot set build console encoding'}
    $env:VSLANG='1033'
    $vcEnvironment=& cmd.exe /d /c "call `"$vcvars`" >nul && set"
    if($LASTEXITCODE){throw 'vcvars failed'}
    foreach($line in $vcEnvironment){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
    $cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    $ninja=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe'
    $argsList=@('-S',$Root,'-B',$BuildDirectory,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_MAKE_PROGRAM=$ninja",'-DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON','-DVEYRA_ENABLE_REMOTEPLAY=ON')
    $refreshDependencies=$false
    $rulesPath=Join-Path $BuildDirectory 'CMakeFiles/rules.ninja'
    $encodingMarker=Join-Path $BuildDirectory 'veyra-utf8-dependencies-v1.txt'
    if((Test-Path -LiteralPath $rulesPath) -and !(Test-Path -LiteralPath $encodingMarker)){
        $refreshDependencies=$true;$argsList+='--fresh'
        Write-Host 'Refreshing MSVC dependency metadata and removing old objects.'
    }
    if($DisplayVersion){$argsList+="-DVEYRA_DISPLAY_VERSION=$DisplayVersion"}
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
    if($refreshDependencies){
        & $cmake --build $BuildDirectory --target clean
        if($LASTEXITCODE){throw 'Cannot clean incompatible build objects'}
    }
    Set-Content -LiteralPath $encodingMarker -Value 'Configure and build with console code page 65001.' -Encoding ascii
    if(!$ConfigureOnly){
        $buildArgs=@('--build',$BuildDirectory,'--parallel','8','--target')+@($Targets)
        & $cmake @buildArgs
        if($LASTEXITCODE){throw "Build failed: $LASTEXITCODE"}
    }
} finally {
    & cmd.exe /d /c "chcp $savedCodePage >nul"
    $env:TEMP=$savedTemp;$env:TMP=$savedTmp;$env:VSLANG=$savedVsLang
}
