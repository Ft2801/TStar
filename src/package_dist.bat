@echo off
setlocal enabledelayedexpansion

REM Check for silent mode (called from build_installer.bat)
set "SILENT_MODE=0"
if "%1"=="--silent" set "SILENT_MODE=1"

if %SILENT_MODE%==0 (
    echo ===========================================
    echo  TStar Distribution Packager
    echo ===========================================
    echo.
)

REM Move to project root (parent directory of this script)
pushd "%~dp0.."

set "BUILD_DIR=build"
set "DIST_DIR=dist\TStar"
set "ERROR_COUNT=0"
set "COPY_COUNT=0"

REM Read version from changelog.txt using shared function
call :GetVersion
echo Version detected: %VERSION%

REM --- Verify build exists ---
call :VerifyFile "%BUILD_DIR%\TStar.exe" "TStar.exe"
if errorlevel 1 (
    echo [ERROR] TStar.exe not found in %BUILD_DIR%
    echo Please run build_all.bat first.
    if %SILENT_MODE%==0 pause
    exit /b 1
)

REM --- Clean old dist ---
echo [STEP 1] Preparing distribution folder...
call :SafeRmDir "dist"
call :EnsureDir "%DIST_DIR%"
if not exist "%DIST_DIR%" (
    echo [ERROR] Failed to create distribution directory
    if %SILENT_MODE%==0 pause
    exit /b 1
)
echo   - Distribution folder created

REM --- Verify/Setup Python ---
if not exist "deps\python\python.exe" (
    echo [INFO] Python environment not found in deps\python.
    echo [INFO] Attempting to run setup_python_dist.ps1...
    powershell -ExecutionPolicy Bypass -File setup_python_dist.ps1
    if not exist "deps\python\python.exe" (
        echo [ERROR] Failed to setup Python environment.
        if %SILENT_MODE%==0 pause
        exit /b 1
    )
)

echo.
echo [STEP 2] Copying main executable...
copy "%BUILD_DIR%\TStar.exe" "%DIST_DIR%\" >nul 2>&1
if exist "%DIST_DIR%\TStar.exe" (
    set /a COPY_COUNT+=1
    echo   - TStar.exe: OK
) else (
    set /a ERROR_COUNT+=1
    echo   [ERROR] TStar.exe: FAILED
)

echo.
echo [STEP 3] Copying Qt DLLs...
setlocal enabledelayedexpansion
for %%f in (Qt6Core.dll Qt6Gui.dll Qt6Network.dll Qt6Svg.dll Qt6Widgets.dll Qt6Xml.dll Qt6Concurrent.dll) do (
    copy "%BUILD_DIR%\%%f" "%DIST_DIR%\" >nul 2>&1
    if exist "%DIST_DIR%\%%f" (
        set /a COPY_COUNT+=1
        echo   - %%f: OK
    ) else (
        set /a ERROR_COUNT+=1
        echo   [ERROR] %%f: FAILED
    )
)
endlocal & set "COPY_COUNT=%COPY_COUNT%" & set "ERROR_COUNT=%ERROR_COUNT%"

echo.
echo [STEP 4] Copying MinGW runtime...
setlocal enabledelayedexpansion
for %%f in (libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll libgomp-1.dll) do (
    copy "%BUILD_DIR%\%%f" "%DIST_DIR%\" >nul 2>&1
    if exist "%DIST_DIR%\%%f" (
        set /a COPY_COUNT+=1
        echo   - %%f: OK
    ) else (
        set /a ERROR_COUNT+=1
        echo   [ERROR] %%f: FAILED
    )
)
endlocal & set "COPY_COUNT=%COPY_COUNT%" & set "ERROR_COUNT=%ERROR_COUNT%"

echo.
echo [STEP 5] Copying OpenGL (optional)...
set "OPENGL_COUNT=0"
for %%f in (opengl32sw.dll D3Dcompiler_47.dll) do (
    if exist "%BUILD_DIR%\%%f" (
        copy "%BUILD_DIR%\%%f" "%DIST_DIR%\" >nul 2>&1
        if exist "%DIST_DIR%\%%f" (
            set /a COPY_COUNT+=1
            set /a OPENGL_COUNT+=1
            echo   - %%f: OK
        )
    )
)
if %OPENGL_COUNT%==0 echo   - No OpenGL DLLs found (optional)

echo.
echo [STEP 6] Copying OpenCV DLLs...
set "OPENCV_COUNT=0"
set "OPENCV_SRC_DIR=deps\opencv\x64\mingw\bin"
if not exist "%OPENCV_SRC_DIR%" set "OPENCV_SRC_DIR=%BUILD_DIR%"

for %%f in ("%OPENCV_SRC_DIR%\libopencv_*.dll") do (
    copy "%%f" "%DIST_DIR%\" >nul 2>&1
    set /a COPY_COUNT+=1
    set /a OPENCV_COUNT+=1
)
if exist "%OPENCV_SRC_DIR%\opencv_videoio_ffmpeg455_64.dll" (
    copy "%OPENCV_SRC_DIR%\opencv_videoio_ffmpeg455_64.dll" "%DIST_DIR%\" >nul 2>&1
    set /a COPY_COUNT+=1
    set /a OPENCV_COUNT+=1
)
echo   - OpenCV DLLs copied: %OPENCV_COUNT%

echo.
echo [STEP 7] Copying GSL DLLs...
set "GSL_SRC_DIR=deps\gsl\bin"
if not exist "%GSL_SRC_DIR%" set "GSL_SRC_DIR=%BUILD_DIR%"

for %%f in (libgsl-28.dll libgslcblas-0.dll) do (
    if exist "%GSL_SRC_DIR%\%%f" (
        copy "%GSL_SRC_DIR%\%%f" "%DIST_DIR%\" >nul 2>&1
        if exist "%DIST_DIR%\%%f" (
            set /a COPY_COUNT+=1
            echo   - %%f: OK
        ) else (
            set /a ERROR_COUNT+=1
            echo   [ERROR] %%f: FAILED
        )
    ) else (
        set /a ERROR_COUNT+=1
        echo   [ERROR] %%f: NOT FOUND IN %GSL_SRC_DIR%
    )
)

echo.
echo [STEP 7.5] Copying LibRaw DLL (optional)...
set "LIBRAW_SRC_DIR=deps\libraw\bin"
if not exist "%LIBRAW_SRC_DIR%" set "LIBRAW_SRC_DIR=%BUILD_DIR%"

if exist "%LIBRAW_SRC_DIR%\libraw.dll" (
    copy "%LIBRAW_SRC_DIR%\libraw.dll" "%DIST_DIR%\" >nul 2>&1
    if exist "%DIST_DIR%\libraw.dll" (
        set /a COPY_COUNT+=1
        echo   - libraw.dll: OK
    ) else (
        echo   - libraw.dll: FAILED to copy
    )
) else (
    echo   - libraw.dll: Not needed (Static)
)

echo.
echo [STEP 8] Copying Qt plugins...
set "PLUGIN_DIRS=platforms styles imageformats iconengines tls networkinformation"
for %%d in (%PLUGIN_DIRS%) do (
    if exist "%BUILD_DIR%\%%d" (
        xcopy "%BUILD_DIR%\%%d" "%DIST_DIR%\%%d\" /E /I /Q >nul 2>&1
        if exist "%DIST_DIR%\%%d" (
            echo   - %%d: OK
        ) else (
            echo   [WARNING] %%d: copy may have failed
        )
    ) else (
        echo   - %%d: not found (optional)
    )
)

echo.
echo [STEP 9] Copying resources...
if exist "src\images" (
    xcopy "src\images" "%DIST_DIR%\images\" /E /I /Q >nul 2>&1
    if exist "%DIST_DIR%\images" (
        echo   - images folder: OK
    ) else (
        echo   [WARNING] images folder: copy may have failed
    )
) else (
    echo   - No images folder found
)

echo.
echo [STEP 9.5] Copying translations...
if exist "%BUILD_DIR%\translations" (
    xcopy "%BUILD_DIR%\translations" "%DIST_DIR%\translations\" /E /I /Q >nul 2>&1
    if exist "%DIST_DIR%\translations" (
        echo   - translations folder: OK
    ) else (
        echo   [WARNING] translations folder: copy may have failed
    )
) else (
    echo   [WARNING] translations folder not found in build directory
)

echo.
echo [STEP 10] Copying Python Environment...
xcopy "deps\python" "%DIST_DIR%\python\" /E /I /Q /EXCLUDE:tools\xcopy_exclude.txt >nul 2>&1
if exist "%DIST_DIR%\python\python.exe" (
    echo   - python folder: OK
) else (
    set /a ERROR_COUNT+=1
    echo   [ERROR] python folder: FAILED
)

echo.
echo [STEP 11] Copying Scripts...
if exist "src\scripts" (
    xcopy "src\scripts" "%DIST_DIR%\scripts\" /E /I /Q /EXCLUDE:tools\xcopy_exclude.txt >nul 2>&1
    if exist "%DIST_DIR%\scripts" (
        echo   - scripts folder: OK
    ) else (
        echo   [WARNING] scripts folder: copy may have failed
    )
) else (
    echo   - No scripts folder found in src\scripts
)

echo.
echo.
echo [STEP 12] Creating README...
(
echo TStar v%VERSION% - Astrophotography Processing Application
echo ============================================================
echo.
echo Just double-click TStar.exe to run!
echo.
echo For external tools ^(Cosmic Clarity, StarNet, GraXpert^):
echo - Configure paths in Settings menu
echo.
echo GitHub: https://github.com/Ft2801/TStar
echo.
) > "%DIST_DIR%\README.txt"
if exist "%DIST_DIR%\README.txt" (
    copy "changelog.txt" "%DIST_DIR%\" >nul 2>&1
echo   - README.txt: OK
echo   - changelog.txt: OK
) else (
    set /a ERROR_COUNT+=1
    echo   [ERROR] README.txt: FAILED
)

echo.
echo ===========================================
echo ===========================================
if not "!ERROR_COUNT!"=="0" goto Error

:Success
echo  SUCCESS! Distribution ready
echo  Files copied: %COPY_COUNT%+
echo  Location: dist\TStar\
echo ===========================================
goto Cleanup

:Error
echo  COMPLETED WITH !ERROR_COUNT! ERROR(S)
echo  Some required files may be missing.
echo ===========================================
if %SILENT_MODE%==0 pause
exit /b 1

:Cleanup
echo.

if %SILENT_MODE%==0 (
    echo To create ZIP: Right-click dist\TStar -^> Send to -^> Compressed folder
    echo.
    pause
)
exit /b 0

REM =============================================================================
REM  Utility Functions (Embedded from windows_utils.bat)
REM =============================================================================

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

