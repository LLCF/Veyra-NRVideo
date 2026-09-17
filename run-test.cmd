@echo off
rem ---------------------------------------------------------------------------
rem Veyra development launcher: incremental build, then run the exe straight out
rem of the build directory. It deliberately produces no portable package and no
rem zip, so it costs zero extra disk.
rem
rem   run-test.cmd                     -> builds, then opens the colour page
rem   run-test.cmd "D:\clip.mp4"       -> builds, then opens that file
rem   run-test.cmd "D:\clip.mp4" --smoke-color --smoke-seconds 20
rem   run-test.cmd --no-build          -> skip the build and just launch
rem ---------------------------------------------------------------------------
setlocal EnableExtensions
pushd "%~dp0"

set "EXE=%CD%\out\build\audio-continuity-repair-20260915\veyra.exe"
set "ARGS=%*"

if /i "%~1"=="--no-build" (
  set "ARGS="
  shift
  :skipbuild_collect
  if not "%~1"=="" (
    set "ARGS=!ARGS! %1"
    shift
    goto skipbuild_collect
  )
  goto launch
)

echo [1/2] incremental build ...
call "%CD%\out\build\veyra-build-x64-release.cmd"
if errorlevel 1 (
  echo.
  echo BUILD FAILED - see the output above.
  popd
  exit /b 1
)

:launch
echo [2/2] launching %EXE%
if "%ARGS%"=="" (
  start "" "%EXE%" --colour-page
) else (
  start "" "%EXE%" %ARGS%
)
popd
exit /b 0
