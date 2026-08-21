@echo off
setlocal enabledelayedexpansion

echo ============================================
echo  DoubleTalk PC SAPI5 - full build
echo ============================================
echo.

set "ROOT=%~dp0"
set "OUTPUT=%ROOT%output"

if not exist "%OUTPUT%" mkdir "%OUTPUT%"

REM ---------------------------------------------------------------- toolchain
where cmake >nul 2>&1
if errorlevel 1 (
    echo ERROR: cmake is not on PATH.
    exit /b 1
)

REM ------------------------------------------------------------------- build
REM Both architectures are built from the same sources. The DoubleTalk emulator
REM ships as a DLL for each, so neither build needs a helper process.

echo [1/4] Configuring and building x64...
cmake -A x64 -S "%ROOT%." -B "%ROOT%build_x64" >nul
if errorlevel 1 ( echo ERROR: x64 configure failed. & exit /b 1 )
cmake --build "%ROOT%build_x64" --config Release
if errorlevel 1 ( echo ERROR: x64 build failed. & exit /b 1 )
echo.

echo [2/4] Configuring and building x86...
cmake -A Win32 -S "%ROOT%." -B "%ROOT%build_x86" >nul
if errorlevel 1 ( echo ERROR: x86 configure failed. & exit /b 1 )
cmake --build "%ROOT%build_x86" --config Release
if errorlevel 1 ( echo ERROR: x86 build failed. & exit /b 1 )
echo.

REM ------------------------------------------------------------- self-test
echo [3/4] Running the engine self-test on both architectures...
"%ROOT%build_x64\bin\Release\dt_render.exe" --self-test
if errorlevel 1 ( echo ERROR: x64 self-test failed. & exit /b 1 )
"%ROOT%build_x86\bin\Release\dt_render.exe" --self-test
if errorlevel 1 ( echo ERROR: x86 self-test failed. & exit /b 1 )
echo.

REM -------------------------------------------------------------- installer
echo [4/4] Building the installer...

REM Inno Setup 6 installs per-machine or per-user depending on how it was
REM installed, so check both rather than assuming Program Files.
set "ISCC="
for %%P in (
    "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
    "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
    "%ProgramFiles%\Inno Setup 6\ISCC.exe"
) do (
    if exist %%P if not defined ISCC set "ISCC=%%~P"
)

if not defined ISCC (
    echo WARNING: Inno Setup 6 not found; skipping the installer.
    echo          Install it with: winget install JRSoftware.InnoSetup
    echo.
    echo Binaries are in build_x64\bin\Release and build_x86\bin\Release.
    exit /b 0
)

echo Using "%ISCC%"
"%ISCC%" /Q "%ROOT%installer\doubletalk.iss"
if errorlevel 1 ( echo ERROR: installer build failed. & exit /b 1 )

echo.
echo ============================================
echo  Build complete.
echo.
dir /b "%OUTPUT%\*.exe"
echo.
echo  Installer: %OUTPUT%
echo ============================================
endlocal
