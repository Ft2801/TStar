@echo off
setlocal enabledelayedexpansion

REM Check for silent mode (called from build_installer.bat)
set "SILENT_MODE=0"
if "%1"=="--silent" set "SILENT_MODE=1"

REM Check for --clean flag
set "CLEAN_MODE=0"
if "%1"=="--clean" set "CLEAN_MODE=1"

REM Check for --lto-on flag
set "LTO_MODE=OFF"
if "%1"=="--lto-on" set "LTO_MODE=ON"
if "%2"=="--lto-on" set "LTO_MODE=ON"



echo ===========================================
echo  TStar Build Script (MinGW + Qt6)
if !CLEAN_MODE!==1 echo  (CLEAN MODE - Reconfiguring CMake)
echo ===========================================

REM Move to project root (parent directory of this script)
pushd "%~dp0.."
set PROJECT_ROOT=%CD%

REM Load shared utilities
if not exist "src\windows_utils.bat" (
    echo [ERROR] src\windows_utils.bat not found!
    exit /b 1
)

REM --- AUTO-DETECT TOOLS ---
call :FindMinGW
if "!MINGW_BIN!"=="" (
    echo [ERROR] MinGW not found. Please install Qt Creator or MinGW separately.
    goto :error
)

call :FindQtPath
if "!QT_PATH!"=="" (
    echo [ERROR] Qt6 not found. Please install Qt6.
    goto :error
)

set CMAKE_CMD=cmake
set CMAKE_GENERATOR=Ninja

REM Check tool availability
if not exist "!MINGW_BIN!\g++.exe" (
    call :LogError "MinGW g++ not found at !MINGW_BIN!"
    echo Please check your Qt installation.
    goto :error
)

REM Add MinGW to PATH for this session
set PATH=!MINGW_BIN!;!PATH!

call :LogInfo "Compiler: !MINGW_BIN!\g++.exe"
call :LogInfo "Qt Path: !QT_PATH!"

REM --- STEP 1: CMAKE CONFIGURATION ---
call :LogStep 1 "Configuring CMake..."

if not exist "build" mkdir "build"

REM Clean CMake cache if requested
if !CLEAN_MODE!==1 (
    call :CleanCMakeCache build
)

REM Configuration step (CMake handles caching itself)

call :LogInfo "Running CMake Configuration..."
"!CMAKE_CMD!" -S . -B build -G "!CMAKE_GENERATOR!" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="!MINGW_BIN!\g++.exe" ^
    -DCMAKE_PREFIX_PATH="!QT_PATH!" ^
    -DENABLE_LTO=!LTO_MODE!
if !errorlevel! neq 0 goto :error

REM --- STEP 2: BUILD ---
:build_step
call :LogStep 2 "Building TStar..."
"!CMAKE_CMD!" --build build --config Release --parallel !NUMBER_OF_PROCESSORS!
if !errorlevel! neq 0 goto :error

REM --- STEP 3: DEPLOY ---
echo.
call :LogStep 3 "Deploying DLLs..."
if !SILENT_MODE!==1 (
    call src\deploy.bat --silent
) else (
    call src\deploy.bat
)

echo.
echo ===========================================
echo  SUCCESS!
echo  Executable: build\TStar.exe
echo ===========================================
exit /b 0

:error
echo.
call :LogError "Build failed"
exit /b 1

REM Include utility functions
:FindMinGW
    set "MINGW_BIN="
    if exist "C:\Qt\Tools\mingw1310_64\bin" set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
    if exist "C:\Qt\Tools\mingw1220_64\bin" set "MINGW_BIN=C:\Qt\Tools\mingw1220_64\bin"
    if exist "C:\msys64\mingw64\bin" set "MINGW_BIN=C:\msys64\mingw64\bin"
    exit /b 0

:FindQtPath
    set "QT_PATH="
    if exist "C:\Qt\6.10.1\mingw_64" set "QT_PATH=C:\Qt\6.10.1\mingw_64"
    if exist "C:\Qt\6.9.2\mingw_64" set "QT_PATH=C:\Qt\6.9.2\mingw_64"
    if exist "C:\Qt\6.8.1\mingw_64" set "QT_PATH=C:\Qt\6.8.1\mingw_64"
    exit /b 0

:LogInfo
    echo [INFO] %~1
    exit /b 0

:LogStep
    echo.
    echo [STEP %1] %~2
    exit /b 0

:LogError
    echo [ERROR] %~1
    exit /b 1

:CleanCMakeCache
    if exist "%~1\CMakeCache.txt" del /q "%~1\CMakeCache.txt"
    if exist "%~1\CMakeFiles" rmdir /s /q "%~1\CMakeFiles"
    echo [INFO] Cleaned CMake cache
    exit /b 0
