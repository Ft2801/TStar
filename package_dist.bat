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

set "BUILD_DIR=build"
set "DIST_DIR=dist\TStar"
set "ERROR_COUNT=0"
set "COPY_COUNT=0"

REM Read version from changelog.txt
set "VERSION=1.0.0"
if exist "changelog.txt" (
    for /f "tokens=2" %%v in ('findstr /R "^Version [0-9.]*" changelog.txt') do (
        set "VERSION=%%v"
        goto :VersionDone
    )
)
:VersionDone

REM --- Verify build exists ---
if not exist "%BUILD_DIR%\TStar.exe" (
    echo [ERROR] TStar.exe not found in %BUILD_DIR%
    echo Please run build_all.bat first.
    if %SILENT_MODE%==0 pause
    exit /b 1
)

REM --- Clean old dist ---
echo [STEP 1] Preparing distribution folder...
if exist dist rmdir /s /q dist
mkdir "%DIST_DIR%"
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
for %%f in (Qt6Core.dll Qt6Gui.dll Qt6Network.dll Qt6Svg.dll Qt6Widgets.dll) do (
    copy "%BUILD_DIR%\%%f" "%DIST_DIR%\" >nul 2>&1
    if exist "%DIST_DIR%\%%f" (
        set /a COPY_COUNT+=1
        echo   - %%f: OK
    ) else (
        set /a ERROR_COUNT+=1
        echo   [ERROR] %%f: FAILED
    )
)

echo.
echo [STEP 4] Copying MinGW runtime...
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
echo [STEP 10] Copying Python Environment...
xcopy "deps\python" "%DIST_DIR%\python\" /E /I /Q >nul 2>&1
if exist "%DIST_DIR%\python\python.exe" (
    echo   - python folder: OK
) else (
    set /a ERROR_COUNT+=1
    echo   [ERROR] python folder: FAILED
)

echo.
echo [STEP 11] Copying Scripts...
if exist "src\scripts" (
    xcopy "src\scripts" "%DIST_DIR%\scripts\" /E /I /Q >nul 2>&1
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
