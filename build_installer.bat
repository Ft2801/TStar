@echo off
setlocal enabledelayedexpansion

echo ===========================================
echo  TStar Installer Builder
echo ===========================================
echo.

REM --- Read version from changelog.txt ---
set "VERSION=1.0.0"
if exist "changelog.txt" (
    for /f "tokens=2" %%v in ('findstr /R "^Version [0-9.]*" changelog.txt') do (
        set "VERSION=%%v"
        goto :VersionDone
    )
) else (
    echo [ERROR] changelog.txt not found!
    pause
    exit /b 1
)
:VersionDone
echo [INFO] Building version: %VERSION%
echo.

REM --- STEP 0: Verify Prerequisites ---
echo [STEP 0] Verifying prerequisites...

REM Check if Inno Setup is installed
set "ISCC="
if exist "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files (x86)\Inno Setup 6\ISCC.exe"
) else if exist "C:\Program Files\Inno Setup 6\ISCC.exe" (
    set "ISCC=C:\Program Files\Inno Setup 6\ISCC.exe"
)

if "!ISCC!"=="" (
    echo [ERROR] Inno Setup 6 not found!
    echo.
    echo Please install Inno Setup from:
    echo   https://jrsoftware.org/isdl.php
    echo.
    echo After installing, run this script again.
    pause
    exit /b 1
)
echo   - Inno Setup: OK

REM Check if installer.iss exists
if not exist "installer.iss" (
    echo [ERROR] installer.iss not found!
    pause
    exit /b 1
)
echo   - installer.iss: OK

REM Check if LICENSE exists
if not exist "LICENSE" (
    echo [ERROR] LICENSE file not found!
    pause
    exit /b 1
)
echo   - LICENSE: OK
echo.

REM --- STEP 1: Clean Previous Installer Output ---
echo [STEP 1] Cleaning previous installer output...
if exist "installer_output" (
    rmdir /s /q "installer_output"
    echo   - Removed old installer_output folder
)
mkdir "installer_output"
echo   - Created fresh installer_output folder
echo.

REM --- STEP 2: Build the Application ---
echo [STEP 2] Building the application...
call build_all.bat --silent
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)
echo   - Build: OK
echo.

REM --- STEP 3: Create Distribution Package ---
echo [STEP 3] Creating distribution package...
call package_dist.bat --silent
if %errorlevel% neq 0 (
    echo [ERROR] Distribution packaging failed!
    pause
    exit /b 1
)

REM Verify distribution was created correctly
if not exist "dist\TStar\TStar.exe" (
    echo [ERROR] Distribution incomplete - TStar.exe not found!
    pause
    exit /b 1
)
echo   - Distribution package: OK
echo.

REM --- STEP 4: Verify Distribution Contents ---
echo [STEP 4] Verifying distribution contents...
set "DIST_DIR=dist\TStar"
set "REQUIRED_FILES=TStar.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll python\python.exe scripts\graxpert_bridge.py"
set "MISS_COUNT=0"

for %%f in (%REQUIRED_FILES%) do (
    if not exist "%DIST_DIR%\%%f" (
        echo   [MISSING] %%f
        set /a MISS_COUNT+=1
    )
)

if not "!MISS_COUNT!"=="0" goto VerificationFailed
echo   - Verification: OK
goto Step5

:VerificationFailed
echo [ERROR] Distribution incomplete.
if "%SILENT_MODE%"=="0" pause
exit /b 1

:Step5
REM Count total files in distribution
set "FILE_COUNT=0"
for /r "%DIST_DIR%" %%f in (*) do set /a FILE_COUNT+=1
echo   - Distribution contains %FILE_COUNT% files
echo   - Required files: OK
echo.

REM --- STEP 5: Create Installer ---
echo [STEP 5] Creating installer with Inno Setup...
echo   - Compiler: !ISCC!
echo   - Script: installer.iss
echo   - Version: %VERSION%
echo.

"!ISCC!" /DMyAppVersion="%VERSION%" installer.iss
if %errorlevel% neq 0 (
    echo.
    echo [ERROR] Installer creation failed!
    echo Check the Inno Setup output above for details.
    pause
    exit /b 1
)
echo.

REM --- STEP 6: Verify Installer Created ---
echo [STEP 6] Verifying installer...
set "INSTALLER_NAME=TStar_Setup_%VERSION%.exe"
set "INSTALLER_PATH=installer_output\%INSTALLER_NAME%"

if not exist "%INSTALLER_PATH%" (
    echo [ERROR] Installer file not found: %INSTALLER_PATH%
    pause
    exit /b 1
)

REM Get installer size
for %%A in ("%INSTALLER_PATH%") do set "INSTALLER_SIZE=%%~zA"
set /a INSTALLER_SIZE_MB=%INSTALLER_SIZE% / 1048576
echo   - Installer created: %INSTALLER_NAME%
echo   - Size: ~%INSTALLER_SIZE_MB% MB
echo.

REM --- SUCCESS ---
echo ===========================================
echo  SUCCESS! Installer Build Complete
echo ===========================================
echo.
echo  Output File:
echo    %INSTALLER_PATH%
echo.
echo  Version: %VERSION%
echo  Size: ~%INSTALLER_SIZE_MB% MB
echo.
echo  Next steps:
echo    1. Test the installer on a clean machine
echo    2. Upload to GitHub Releases
echo.
echo ===========================================
pause
exit /b 0
