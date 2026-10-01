@echo off
setlocal
cd /d "%~dp0.."
if errorlevel 1 exit /b 1

if not defined VSCMD_VER (
    for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do call "%%i\VC\Auxiliary\Build\vcvars64.bat"
    if errorlevel 1 exit /b 1
)
if not defined VSCMD_VER (
    echo MSVC x64 environment unavailable.
    exit /b 1
)
set "PATH=%ProgramFiles%\CMake\bin;%PATH%"
set "EQVK_NINJA="
for /f "delims=" %%i in ('where ninja.exe 2^>nul') do if not defined EQVK_NINJA set "EQVK_NINJA=%%i"
if not defined EQVK_NINJA for /d %%i in ("%CD%\vcpkg\downloads\tools\ninja-*-windows") do if exist "%%i\ninja.exe" set "EQVK_NINJA=%%i\ninja.exe"
if not defined EQVK_NINJA (
    echo Ninja unavailable. Install Ninja or complete vcpkg tool bootstrap.
    exit /b 1
)
cmake --preset msvc-x64 "-DCMAKE_MAKE_PROGRAM=%EQVK_NINJA%"
if errorlevel 1 exit /b 1
cmake --build --preset msvc-x64-relwithdebinfo
exit /b %errorlevel%
