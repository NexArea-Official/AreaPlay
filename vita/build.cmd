@echo off
rem ============================================================
rem  Builds Area Play (Vita Edition) -> build\AreaPlay.vpk
rem  Needs VitaSDK (https://vitasdk.org), CMake and Ninja.
rem  Set VITASDK to your VitaSDK folder before running.
rem  "build.cmd clean" rebuilds from scratch.
rem ============================================================
cd /d "%~dp0"
if not defined VITASDK (
  echo ERROR: the VITASDK environment variable is not set.
  pause
  exit /b 1
)
set "PATH=%VITASDK%\bin;%PATH%"

if /i "%1"=="clean" if exist build rmdir /s /q build

cmake -G Ninja -S . -B build -DCMAKE_TOOLCHAIN_FILE="%VITASDK%\share\vita.toolchain.cmake"
if errorlevel 1 goto fail

rem The VPK step can fail silently: remove the old VPK first.
if exist "build\AreaPlay.vpk.out" del /q "build\AreaPlay.vpk.out"
if exist "build\AreaPlay.vpk" del /q "build\AreaPlay.vpk"

cmake --build build
if errorlevel 1 goto fail
if not exist "build\AreaPlay.vpk" goto fail

echo.
echo Done: %CD%\build\AreaPlay.vpk
if /i not "%1"=="nopause" pause
exit /b 0

:fail
echo.
echo BUILD FAILED (see the messages above).
if /i not "%1"=="nopause" pause
exit /b 1
