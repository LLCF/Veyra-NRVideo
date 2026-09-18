[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$SourceDirectory,
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$SdkRoot,
    [Parameter(Mandatory)][string]$OfficialDll,
    [Parameter(Mandatory)][string]$Dxc,
    [Parameter(Mandatory)][string]$TempDirectory,
    [string]$Python='python',
    [switch]$ReuseGenerated
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$revision='88635b94083965a7c3b5f64e099808b8ba2ce576'
$artifactRoot=[IO.Path]::GetFullPath('E:/项目/Veyra/').TrimEnd('\')+'\'
foreach($directory in @($SourceDirectory,$BuildDirectory,$TempDirectory)) {
    if(![IO.Path]::GetFullPath($directory).StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Provider source, build and temporary data must stay below E:/项目/Veyra/'
    }
}
if(![IO.File]::Exists($OfficialDll)){throw 'Supply the local official provider; this script does not download it'}
$head=& git -C $SourceDirectory rev-parse HEAD
if($LASTEXITCODE -or $head -ne $revision){throw "Expected upstream $revision"}
$patch=Join-Path $PSScriptRoot 'provider-veyra.patch'
& git -C $SourceDirectory apply --reverse --check $patch 2>$null
if($LASTEXITCODE){
    & git -C $SourceDirectory apply --check $patch
    if($LASTEXITCODE){throw 'Patch conflicts with the external checkout'}
    & git -C $SourceDirectory apply $patch
    if($LASTEXITCODE){throw 'Patch failed'}
}
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs=(& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
if(!$vs){throw 'MSVC is required'}
$cmake=Join-Path $vs 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$savedTemp=$env:TEMP;$savedTmp=$env:TMP
New-Item -ItemType Directory -Force -Path $BuildDirectory,$TempDirectory | Out-Null
try {
    $env:TEMP=$TempDirectory;$env:TMP=$TempDirectory
    if(!$ReuseGenerated){
        & $Python (Join-Path $SourceDirectory 'bench/tools/build_411_dll.py') --dll $OfficialDll --ffx-sdk-root $SdkRoot --dxc $Dxc --build-dir $BuildDirectory --codegen-only
        if($LASTEXITCODE){throw 'Provider code generation failed'}
    }
    & $cmake -S (Join-Path $SourceDirectory 'bench/provider411') -B $BuildDirectory -G 'Visual Studio 17 2022' -A x64 "-DFFX_SDK_ROOT=$SdkRoot" "-DFSR411_GEN_DIR=$BuildDirectory/gen"
    if($LASTEXITCODE){throw 'Provider configure failed'}
    & $cmake --build $BuildDirectory --config Release --parallel 4
    if($LASTEXITCODE){throw 'Provider build failed'}
    & (Join-Path $BuildDirectory 'Release/ffx411_smoke.exe') (Join-Path $BuildDirectory 'Release/amd_fidelityfx_upscaler_dx12.dll')
    if($LASTEXITCODE){throw 'CPU API smoke failed'}
} finally {$env:TEMP=$savedTemp;$env:TMP=$savedTmp}
