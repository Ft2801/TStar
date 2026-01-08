@echo off
REM =============================================================================
REM TStar Windows Build Utilities
REM Shared functions for build_all.bat, package_dist.bat, build_installer.bat
REM =============================================================================

setlocal enabledelayedexpansion

REM --- Version Management ---
REM Call this to get the current version
:GetVersion
set "VERSION=1.0.0"
if exist "changelog.txt" (
    for /f "tokens=2" %%v in ('type "changelog.txt" ^| findstr /R "^Version [0-9]"') do (
        set "VERSION=%%v"
        goto :EOF
    )
)
goto :EOF

REM --- Tool Detection ---
:FindInnoSetup
set "ISCC="
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
    goto :EOF
)
if exist "C:\Program Files\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
    goto :EOF
)
REM Fallback to searching registry
for /f "tokens=2*" %%A in ('reg query "HKLM\Software\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1" /v "InstallLocation" 2^>nul') do (
    if exist "%%B\ISCC.exe" (
        set "ISCC=%%B\ISCC.exe"
        goto :EOF
    )
)
goto :EOF

:FindMinGW
set "MINGW_BIN="
REM Check common Qt installation paths
if exist "C:\Qt\Tools\mingw1310_64\bin" (
    set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
    goto :EOF
)
if exist "C:\Qt\Tools\mingw1220_64\bin" (
    set "MINGW_BIN=C:\Qt\Tools\mingw1220_64\bin"
    goto :EOF
)
if exist "C:\msys64\mingw64\bin" (
    set "MINGW_BIN=C:\msys64\mingw64\bin"
    goto :EOF
)
REM Fallback to PATH search
for %%X in (g++.exe) do (
    set "GXX_PATH=%%~$PATH:X"
    if not "!GXX_PATH!"=="" (
        for %%A in ("!GXX_PATH!\.") do set "MINGW_BIN=%%~fA"
        goto :EOF
    )
)
goto :EOF

:FindQtPath
set "QT_PATH="
REM Check common Qt locations
if exist "C:\Qt\6.10.1\mingw_64" (
    set "QT_PATH=C:\Qt\6.10.1\mingw_64"
    goto :EOF
)
if exist "C:\Qt\6.9.2\mingw_64" (
    set "QT_PATH=C:\Qt\6.9.2\mingw_64"
    goto :EOF
)
if exist "C:\Qt\6.8.1\mingw_64" (
    set "QT_PATH=C:\Qt\6.8.1\mingw_64"
    goto :EOF
)
REM Fallback to qmake search
for %%X in (qmake.exe) do (
    set "QMAKE_PATH=%%~$PATH:X"
    if not "!QMAKE_PATH!"=="" (
        for %%A in ("!QMAKE_PATH!\..\..") do set "QT_PATH=%%~fA"
        goto :EOF
    )
)
goto :EOF

REM --- Validation Functions ---
:VerifyFile
REM Usage: call :VerifyFile path description
REM Returns: errorlevel 0 if exists, 1 if not
set "FILE_PATH=%~1"
set "DESCRIPTION=%~2"
if not exist "!FILE_PATH!" (
    echo [ERROR] !DESCRIPTION! not found: !FILE_PATH!
    exit /b 1
)
exit /b 0

:VerifyDir
REM Usage: call :VerifyDir path description
set "DIR_PATH=%~1"
set "DESCRIPTION=%~2"
if not exist "!DIR_PATH!" (
    echo [ERROR] !DESCRIPTION! not found: !DIR_PATH!
    exit /b 1
)
exit /b 0

:VerifyCommand
REM Usage: call :VerifyCommand command description
set "COMMAND=%~1"
set "DESCRIPTION=%~2"
where /q "!COMMAND!"
if %errorlevel% neq 0 (
    echo [ERROR] !DESCRIPTION! not found in PATH
    exit /b 1
)
exit /b 0

REM --- Path Utilities ---
:NormalizePath
REM Usage: call :NormalizePath path_var
REM Updates the variable with normalized path
for %%A in ("%~1") do set "%~1=%%~fA"
exit /b 0

REM --- Directory Operations ---
:SafeRmDir
REM Usage: call :SafeRmDir path
REM Safely removes directory if it exists
if exist "%~1" (
    rmdir /s /q "%~1"
)
exit /b 0

:EnsureDir
REM Usage: call :EnsureDir path
REM Creates directory if it doesn't exist
if not exist "%~1" mkdir "%~1"
exit /b 0

REM --- Logging Functions ---
:LogInfo
echo [INFO] %~1
exit /b 0

:LogWarning
echo [WARNING] %~1
exit /b 0

:LogError
echo [ERROR] %~1
exit /b 1

:LogStep
echo.
echo [STEP %1] %~2
exit /b 0

REM --- File Operations ---
:CopyWithCheck
REM Usage: call :CopyWithCheck source destination description
REM Returns: 0 if success, 1 if failed
set "SRC=%~1"
set "DST=%~2"
set "DESC=%~3"
copy "!SRC!" "!DST!" >nul 2>&1
if exist "!DST!" (
    echo   - !DESC!: OK
    exit /b 0
) else (
    echo   [ERROR] !DESC!: FAILED
    exit /b 1
)

REM --- Python Environment ---
:SetupPythonVenv
REM Usage: call :SetupPythonVenv dest_dir
REM Returns: 0 if success, 1 if failed
set "VENV_DIR=%~1"
set "PYTHON_CMD=python"

if not exist "!VENV_DIR!" (
    echo [INFO] Creating Python virtual environment...
    "!PYTHON_CMD!" -m venv "!VENV_DIR!"
    if not exist "!VENV_DIR!" (
        echo [ERROR] Failed to create Python virtual environment
        exit /b 1
    )
)

echo [INFO] Upgrading pip...
"!VENV_DIR!\Scripts\python.exe" -m pip install --upgrade pip --quiet >nul 2>&1

exit /b 0

REM --- Dependency Installation ---
:InstallPythonPackage
REM Usage: call :InstallPythonPackage venv_dir package_name
set "VENV_DIR=%~1"
set "PACKAGE=%~2"
"!VENV_DIR!\Scripts\python.exe" -m pip install "!PACKAGE!" --quiet >nul 2>&1
if !errorlevel! neq 0 (
    echo   [ERROR] Failed to install !PACKAGE!
    exit /b 1
)
echo   - !PACKAGE!: OK
exit /b 0

REM --- Build Configuration ---
:ConfigureCMake
REM Usage: call :ConfigureCMake build_dir generator cmake_args
REM Expected variables: CMAKE_CMD, PROJECT_ROOT
set "BUILD_DIR=%~1"
set "GENERATOR=%~2"
shift
shift
set "CMAKE_ARGS=%*"

if not exist "!BUILD_DIR!" mkdir "!BUILD_DIR!"

if exist "!BUILD_DIR!\CMakeCache.txt" (
    echo [INFO] CMakeCache.txt found. Skipping configuration.
    exit /b 0
)

echo [INFO] Configuring CMake with !GENERATOR!...
"!CMAKE_CMD!" -S "!PROJECT_ROOT!" -B "!BUILD_DIR!" -G "!GENERATOR!" !CMAKE_ARGS!
if !errorlevel! neq 0 (
    echo [ERROR] CMake configuration failed
    exit /b 1
)
exit /b 0

REM --- Cleanup Functions ---
:CleanCMakeCache
REM Usage: call :CleanCMakeCache build_dir
set "BUILD_DIR=%~1"
if exist "!BUILD_DIR!\CMakeCache.txt" del /q "!BUILD_DIR!\CMakeCache.txt"
if exist "!BUILD_DIR!\CMakeFiles" rmdir /s /q "!BUILD_DIR!\CMakeFiles"
echo [INFO] Cleaned CMake cache
exit /b 0
