@echo off
setlocal enabledelayedexpansion

REM Check for silent mode (called from build_installer.bat)
set "SILENT_MODE=0"
if "%1"=="--silent" set "SILENT_MODE=1"

echo ===========================================
echo  TStar Build Script (MinGW + Qt6)
echo ===========================================

REM --- CONFIGURATION ---
REM Adjust these if your paths differ
set MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin
set QT_PATH=C:\Qt\6.10.1\mingw_64
set CMAKE_CMD=cmake
set CMAKE_GENERATOR=Ninja

REM Check tool availability
if not exist "%MINGW_BIN%\g++.exe" (
    echo [ERROR] MinGW g++ not found at %MINGW_BIN%
    echo Please edit this script to set the correct MINGW_BIN path.
    goto :error
)

REM Add MinGW to PATH for this session
set PATH=%MINGW_BIN%;%PATH%

REM --- 1. PREPARE ENVIRONMENT ---
echo [INFO] Compiler: %MINGW_BIN%\g++.exe
echo [INFO] Qt Path:  %QT_PATH%

REM --- 2. BUILD TSTAR ---
echo.
echo [STEP 2] Building TStar (Portable Mode)...
if not exist "build" mkdir "build"

if exist "build\CMakeCache.txt" goto :skip_config

echo [INFO] Running CMake Configuration...
"%CMAKE_CMD%" -S . -B build -G "%CMAKE_GENERATOR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="%MINGW_BIN%\g++.exe" ^
    -DCMAKE_PREFIX_PATH="%QT_PATH%" 
if %errorlevel% neq 0 goto :error

:skip_config
echo [INFO] Skip Config (CMakeCache found). Building...
:build_step
"%CMAKE_CMD%" --build build --config Release --parallel %NUMBER_OF_PROCESSORS%
if %errorlevel% neq 0 goto :error

echo.
echo [STEP 3] Deployment (copying DLLs)...
if !SILENT_MODE!==1 (
    call deploy.bat --silent
) else (
    call deploy.bat
)

echo.
echo ===========================================
echo  SUCCESS!
echo  Executable: build\TStar.exe
echo ===========================================
if %errorlevel% neq 0 exit /b 0
exit /b 0

:error
echo.
echo [ERROR] Build failed.
if %errorlevel% neq 0 exit /b 1
exit /b 1
