@echo off
setlocal enabledelayedexpansion

echo ===========================================
echo  TStar Installer Builder
echo ===========================================
echo.

REM Move to project root (parent directory of this script)
pushd "%~dp0.."

REM --- Read version from changelog.txt ---
call :GetVersion
echo [INFO] Building version: %VERSION%
echo.

REM --- STEP 0: Verify Prerequisites ---
echo [STEP 0] Verifying prerequisites...

REM Check if Inno Setup is installed
call :FindInnoSetup
if "!ISCC!"=="" (
    echo [ERROR] Inno Setup 6 not found!
    echo.
    echo Please install Inno Setup from:
    echo   https://jrsoftware.org/isdl.php
    echo.
    echo After installing, run this script again.
) else (
    echo   - Inno Setup 6: OK at !ISCC!
)

REM Check changelog.txt exists
call :VerifyFile "changelog.txt" "changelog.txt"
if errorlevel 1 (
    pause
    exit /b 1
)
echo   - Inno Setup: OK

REM Check if installer.iss exists (in src)
if not exist "src\installer.iss" (
    echo [ERROR] src\installer.iss not found!
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

REM --- STEP 0.5: Prepare Assets ---
echo [STEP 0.5] Preparing assets...
if exist "tools\convert_icon.py" (
    python tools\convert_icon.py
    call :VerifyFile "src\images\Logo.ico" "Icon conversion"
    if errorlevel 0 echo   - Icon converted: OK
    
    if exist "tools\convert_wizard_images.py" (
        python tools\convert_wizard_images.py
        echo   - Wizard images converted to BMP
    )
)
echo.

REM --- STEP 1: Clean Previous Installer Output ---
echo [STEP 1] Cleaning previous installer output...
call :SafeRmDir "installer_output"
call :EnsureDir "installer_output"
echo   - Created fresh installer_output folder
echo.

REM --- STEP 2: Build the Application ---
echo [STEP 2] Building the application...
call src\build_all.bat --silent
if %errorlevel% neq 0 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)
echo   - Build: OK
echo.

REM --- STEP 3: Create Distribution Package ---
echo [STEP 3] Creating distribution package...
call src\package_dist.bat --silent
if %errorlevel% neq 0 (
    echo [ERROR] Distribution packaging failed!
    pause
    exit /b 1
)

REM Verify distribution was created correctly
call :VerifyFile "dist\TStar\TStar.exe" "Distribution TStar.exe"
if errorlevel 1 (
    echo [ERROR] Distribution incomplete!
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
echo   - Script: src\installer.iss
echo   - Version: %VERSION%
echo.

"!ISCC!" /DMyAppVersion="%VERSION%" src\installer.iss
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

call :VerifyFile "%INSTALLER_PATH%" "Installer file"
if errorlevel 1 (
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

:GetVersion
set "VERSION=1.0.0"
if exist "changelog.txt" (
    for /f "tokens=2" %%v in ('type "changelog.txt" ^| findstr /R "^Version [0-9]"') do (
        set "VERSION=%%v"
        goto :EOF
    )
)
goto :EOF

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

:VerifyFile
REM Usage: call :VerifyFile path description
set "FILE_PATH=%~1"
set "DESCRIPTION=%~2"
if not exist "!FILE_PATH!" (
    echo [ERROR] !DESCRIPTION! not found: !FILE_PATH!
    exit /b 1
)
exit /b 0

:VerifyDir
set "DIR_PATH=%~1"
set "DESCRIPTION=%~2"
if not exist "!DIR_PATH!" (
    echo [ERROR] !DESCRIPTION! not found: !DIR_PATH!
    exit /b 1
)
exit /b 0

:SafeRmDir
if exist "%~1" (
    rmdir /s /q "%~1"
)
exit /b 0

:EnsureDir
if not exist "%~1" mkdir "%~1"
exit /b 0
