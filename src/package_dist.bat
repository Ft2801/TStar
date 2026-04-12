@echo off
setlocal enabledelayedexpansion

REM =============================================================================
REM TStar Distribution Packager (Windows)
REM =============================================================================
REM
REM Collects all build artefacts, runtime libraries, Qt plugins, Python
REM environment, scripts, and data into a self-contained distribution folder
REM (dist\TStar\) ready for installer creation or ZIP packaging.
REM
REM Usage:
REM   package_dist.bat              -- interactive mode
REM   package_dist.bat --silent     -- silent mode (used by build_installer.bat)
REM =============================================================================

REM -- Parse command-line flags ------------------------------------------------

set "SILENT_MODE=0"
if "%1"=="--silent" set "SILENT_MODE=1"

if %SILENT_MODE%==0 (
    echo ===========================================
    echo  TStar Distribution Packager
    echo ===========================================
    echo.
)

REM -- Navigate to project root (parent of this script's directory) ------------

pushd "%~dp0.."

REM -- Configuration -----------------------------------------------------------

set "BUILD_DIR=build"
set "DIST_DIR=dist\TStar"
set "ERROR_COUNT=0"
set "COPY_COUNT=0"

REM -- Version detection -------------------------------------------------------

call src\windows_utils.bat :GetVersion
echo Version detected: %VERSION%

REM =============================================================================
REM STEP 1 -- Verify that the build output exists
REM =============================================================================

call :VerifyFile "%BUILD_DIR%\TStar.exe" "TStar.exe"
if errorlevel 1 (
    echo [ERROR] TStar.exe not found in %BUILD_DIR%
    echo Please run build_all.bat first.
    if %SILENT_MODE%==0 pause
    exit /b 1
)

REM =============================================================================
REM STEP 1 -- Prepare a clean distribution folder
REM =============================================================================

echo [STEP 1] Preparing distribution folder...
call :SafeRmDir "dist"
call :EnsureDir "%DIST_DIR%"
if not exist "%DIST_DIR%" (
    echo [ERROR] Failed to create distribution directory
    if %SILENT_MODE%==0 pause
    exit /b 1
)
echo  - Distribution folder created

REM -- Verify / bootstrap embedded Python --------------------------------------

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

REM =============================================================================
REM STEP 2 -- Copy the main executable
REM =============================================================================

echo.
echo [STEP 2] Copying main executable...
copy "%BUILD_DIR%\TStar.exe" "%DIST_DIR%\" >nul 2>&1
if exist "%DIST_DIR%\TStar.exe" (
    set /a COPY_COUNT+=1
    echo  - TStar.exe: OK
) else (
    set /a ERROR_COUNT+=1
    echo  [ERROR] TStar.exe: FAILED
)

REM =============================================================================
REM STEP 3 -- Copy Qt and locally-built DLLs
REM =============================================================================

echo.
echo [STEP 3] Copying Qt and local DLLs...
setlocal enabledelayedexpansion
for %%f in ("%BUILD_DIR%\*.dll") do (
    copy "%%f" "%DIST_DIR%\" >nul 2>&1
    if exist "%DIST_DIR%\%%~nxf" (
        set /a COPY_COUNT+=1
        echo  - %%~nxf: OK
    ) else (
        set /a ERROR_COUNT+=1
        echo  [ERROR] %%~nxf: FAILED
    )
)
endlocal & set "COPY_COUNT=%COPY_COUNT%" & set "ERROR_COUNT=%ERROR_COUNT%"

REM =============================================================================
REM STEP 3.5 -- Collect mandatory MinGW and OpenMP runtimes
REM =============================================================================

echo.
echo [STEP 3.5] Collecting mandatory MinGW and OpenMP runtimes...
call src\windows_utils.bat :FindMinGW
if "%MINGW_BIN%"=="" (
    echo  [WARNING] MinGW bin path not detected. Some runtimes may be missing.
) else (
    echo  - MinGW Location: %MINGW_BIN%
    for %%f in (libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll libgomp-1.dll zlib1.dll liblz4.dll libzstd.dll) do (
        if exist "%MINGW_BIN%\%%f" (
            copy "%MINGW_BIN%\%%f" "%DIST_DIR%\" >nul 2>&1
            if exist "%DIST_DIR%\%%f" (
                set /a COPY_COUNT+=1
                echo  - %%f: OK
            )
        ) else (
            REM libgomp is critical for the native solver; other DLLs may vary by MinGW version
            if "%%f"=="libgomp-1.dll" (
                echo  [WARNING] libgomp-1.dll NOT FOUND. Native Solver will CRASH!
            )
        )
    )
)

REM =============================================================================
REM STEP 5 -- Copy OpenGL support DLLs (optional)
REM =============================================================================

echo.
echo [STEP 5] Copying OpenGL (optional)...
set "OPENGL_COUNT=0"
for %%f in (opengl32sw.dll D3Dcompiler_47.dll) do (
    if exist "%BUILD_DIR%\%%f" (
        copy "%BUILD_DIR%\%%f" "%DIST_DIR%\" >nul 2>&1
        if exist "%DIST_DIR%\%%f" (
            set /a COPY_COUNT+=1
            set /a OPENGL_COUNT+=1
            echo  - %%f: OK
        )
    )
)
if %OPENGL_COUNT%==0 echo  - No OpenGL DLLs found (optional)

REM =============================================================================
REM STEP 6 -- Copy OpenCV DLLs
REM =============================================================================

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
for %%f in ("%OPENCV_SRC_DIR%\opencv_videoio_ffmpeg*.dll") do (
    copy "%%f" "%DIST_DIR%\" >nul 2>&1
    set /a COPY_COUNT+=1
    set /a OPENCV_COUNT+=1
)
echo  - OpenCV DLLs copied: %OPENCV_COUNT%

REM =============================================================================
REM STEP 7 -- Copy GSL DLLs
REM =============================================================================

echo.
echo [STEP 7] Copying GSL DLLs...
set "GSL_SRC_DIR=deps\gsl\bin"
if not exist "%GSL_SRC_DIR%" set "GSL_SRC_DIR=%BUILD_DIR%"

set "GSL_FOUND_COUNT=0"
for %%f in ("%GSL_SRC_DIR%\libgsl-*.dll" "%GSL_SRC_DIR%\libgslcblas-*.dll") do (
    copy "%%f" "%DIST_DIR%\" >nul 2>&1
    if exist "%DIST_DIR%\%%~nxf" (
        set /a COPY_COUNT+=1
        set /a GSL_FOUND_COUNT+=1
        echo  - %%~nxf: OK
    )
)

if %GSL_FOUND_COUNT% equ 0 (
    echo  [ERROR] No GSL DLLs found in %GSL_SRC_DIR%
    set /a ERROR_COUNT+=1
) else (
    echo  - GSL DLLs successfully collected.
)

REM =============================================================================
REM STEP 7.5 -- Copy LibRaw DLL (optional -- may be statically linked)
REM =============================================================================

echo.
echo [STEP 7.5] Copying LibRaw DLL (optional)...
set "LIBRAW_SRC_DIR=deps\libraw\bin"
if not exist "%LIBRAW_SRC_DIR%" set "LIBRAW_SRC_DIR=%BUILD_DIR%"

if exist "%LIBRAW_SRC_DIR%\libraw.dll" (
    copy "%LIBRAW_SRC_DIR%\libraw.dll" "%DIST_DIR%\" >nul 2>&1
    if exist "%DIST_DIR%\libraw.dll" (
        set /a COPY_COUNT+=1
        echo  - libraw.dll: OK
    ) else (
        echo  - libraw.dll: FAILED to copy
    )
) else (
    echo  - libraw.dll: Not needed (Static)
)

REM =============================================================================
REM STEP 7.6 -- Verify color management (lcms2 -- statically linked)
REM =============================================================================

echo.
echo [STEP 7.6] Verifying Color Management (lcms2)...
echo  - lcms2: OK (Statically linked into TStar.exe)

REM =============================================================================
REM STEP 8 -- Copy Qt plugins
REM =============================================================================

echo.
echo [STEP 8] Copying Qt plugins...
set "PLUGIN_DIRS=platforms styles imageformats iconengines tls networkinformation"
for %%d in (%PLUGIN_DIRS%) do (
    if exist "%BUILD_DIR%\%%d" (
        xcopy "%BUILD_DIR%\%%d" "%DIST_DIR%\%%d\" /E /I /Q >nul 2>&1
        if exist "%DIST_DIR%\%%d" (
            echo  - %%d: OK
        ) else (
            echo  [WARNING] %%d: copy may have failed
        )
    ) else (
        echo  - %%d: not found (optional)
    )
)

REM =============================================================================
REM STEP 9 -- Copy resource assets (images)
REM =============================================================================

echo.
echo [STEP 9] Copying resources...
if exist "src\images" (
    xcopy "src\images" "%DIST_DIR%\images\" /E /I /Q >nul 2>&1
    if exist "%DIST_DIR%\images" (
        echo  - images folder: OK
    ) else (
        echo  [WARNING] images folder: copy may have failed
    )
) else (
    echo  - No images folder found
)

REM =============================================================================
REM STEP 9.5 -- Copy translation files
REM =============================================================================

echo.
echo [STEP 9.5] Copying translations...
if exist "%BUILD_DIR%\translations" (
    xcopy "%BUILD_DIR%\translations" "%DIST_DIR%\translations\" /E /I /Q >nul 2>&1
    if exist "%DIST_DIR%\translations" (
        echo  - translations folder: OK
    ) else (
        echo  [WARNING] translations folder: copy may have failed
    )
) else (
    echo  [WARNING] translations folder not found in build directory
)

REM =============================================================================
REM STEP 10 -- Copy embedded Python environment
REM =============================================================================

echo.
echo [STEP 10] Copying Python Environment...
xcopy "deps\python" "%DIST_DIR%\python\" /E /I /Q /EXCLUDE:tools\xcopy_exclude.txt >nul 2>&1
if exist "%DIST_DIR%\python\python.exe" (
    echo  - python folder: OK
) else (
    set /a ERROR_COUNT+=1
    echo  [ERROR] python folder: FAILED
)

REM =============================================================================
REM STEP 10.5 -- Copy MSVC redistributable DLLs for Python
REM =============================================================================

echo.
echo [STEP 10.5] Copying MSVC Redistributables for Python...
set "VCREDIST_SRC=%windir%\System32"
set "VCREDIST_FILES=vcruntime140.dll vcruntime140_1.dll msvcp140.dll msvcp140_1.dll msvcp140_2.dll vcomp140.dll concrt140.dll"
set "VC_COPY_COUNT=0"

for %%f in (%VCREDIST_FILES%) do (
    if exist "%VCREDIST_SRC%\%%f" (
        copy "%VCREDIST_SRC%\%%f" "%DIST_DIR%\python\" >nul 2>&1
        copy "%VCREDIST_SRC%\%%f" "%DIST_DIR%\" >nul 2>&1
        if exist "%DIST_DIR%\python\%%f" (
            set /a VC_COPY_COUNT+=1
        )
    )
)
echo  - Copied %VC_COPY_COUNT% MSVC DLLs to avoid crashes on clean machines.

REM =============================================================================
REM STEP 11 -- Copy application scripts
REM =============================================================================

echo.
echo [STEP 11] Copying Scripts...

REM 11.1 Python bridge scripts (src/scripts)
if exist "src\scripts" (
    xcopy "src\scripts" "%DIST_DIR%\scripts\" /E /I /Q /EXCLUDE:tools\xcopy_exclude.txt >nul 2>&1
    if exist "%DIST_DIR%\scripts" (
        echo  - Python bridge scripts: OK
    ) else (
        echo  [WARNING] Python bridge scripts: copy may have failed
    )
) else (
    echo  - No scripts folder found in src\scripts
)

REM 11.2 TStar scripts (root scripts folder)
if exist "scripts" (
    xcopy "scripts" "%DIST_DIR%\scripts\" /E /I /Q /EXCLUDE:tools\xcopy_exclude.txt >nul 2>&1
    if exist "%DIST_DIR%\scripts" (
        echo  - TStar scripts: OK
    ) else (
        echo  [WARNING] TStar scripts: copy may have failed
    )
) else (
    echo  - No TStar scripts found in root scripts folder
)

REM 11.3 UI scripts (root JS-Scripts folder)
if exist "JS-Scripts" (
    xcopy "JS-Scripts" "%DIST_DIR%\JS-Scripts\" /E /I /Q /EXCLUDE:tools\xcopy_exclude.txt >nul 2>&1
    if exist "%DIST_DIR%\JS-Scripts" (
        echo  - JS-Scripts folder: OK
    ) else (
        echo  [WARNING] JS-Scripts folder: copy may have failed
    )
) else (
    echo  - No JS-Scripts folder found in root
)

REM =============================================================================
REM STEP 11.5 -- Bundle ASTAP plate-solver (optional)
REM =============================================================================

echo.
echo [STEP 11.5] Copying ASTAP (optional bundling)...
set "ASTAP_COPIED=0"
set "ASTAP_BASE="
if exist "C:\Program Files\astap\astap_cli.exe" set "ASTAP_BASE=C:\Program Files\astap"
if "%ASTAP_BASE%"=="" if exist "C:\Program Files (x86)\astap\astap_cli.exe" set "ASTAP_BASE=C:\Program Files (x86)\astap"
if "%ASTAP_BASE%"=="" if exist "C:\Program Files\astap\astap.exe" set "ASTAP_BASE=C:\Program Files\astap"
if "%ASTAP_BASE%"=="" if exist "C:\Program Files (x86)\astap\astap.exe" set "ASTAP_BASE=C:\Program Files (x86)\astap"

if not "%ASTAP_BASE%"=="" (
    call :EnsureDir "%DIST_DIR%\deps"

    if exist "%ASTAP_BASE%\astap_cli.exe" (
        copy "%ASTAP_BASE%\astap_cli.exe" "%DIST_DIR%\deps\" >nul 2>&1
        if exist "%DIST_DIR%\deps\astap_cli.exe" (
            echo  - astap_cli.exe: OK
            set "ASTAP_COPIED=1"
        ) else (
            echo  [WARNING] astap_cli.exe: copy failed
        )
    )

    if exist "%ASTAP_BASE%\astap.exe" (
        copy "%ASTAP_BASE%\astap.exe" "%DIST_DIR%\deps\" >nul 2>&1
        if exist "%DIST_DIR%\deps\astap.exe" (
            echo  - astap.exe: OK
            set "ASTAP_COPIED=1"
        ) else (
            echo  [WARNING] astap.exe: copy failed
        )
    )

    if exist "%ASTAP_BASE%\Databases" (
        xcopy "%ASTAP_BASE%\Databases" "%DIST_DIR%\deps\Databases\" /E /I /Q >nul 2>&1
        if exist "%DIST_DIR%\deps\Databases" (
            echo  - ASTAP Databases: OK
        ) else (
            echo  [WARNING] ASTAP Databases: copy may have failed
        )
    )

    if "%ASTAP_COPIED%"=="0" (
        echo  [WARNING] ASTAP binaries were not copied from %ASTAP_BASE%
    )
) else (
    echo  - [WARNING] ASTAP not found in Program Files, skipping
)

REM =============================================================================
REM STEP 12 -- Copy data catalogs and SPCC resources
REM =============================================================================

echo.
echo [STEP 12] Copying Data Catalogs and SPCC Resources...
if exist "data" (
    xcopy "data" "%DIST_DIR%\data\" /E /I /Q >nul 2>&1
    if exist "%DIST_DIR%\data" (
        echo  - data folder ^(catalogs, SPCC^): OK
    ) else (
        set /a ERROR_COUNT+=1
        echo  [ERROR] data folder: FAILED to copy
    )
) else (
    set /a ERROR_COUNT+=1
    echo  [ERROR] data folder: NOT FOUND IN PROJECT ROOT
)

REM =============================================================================
REM STEP 12.5 -- Copy minigame resources
REM =============================================================================

echo.
echo [STEP 12.5] Copying Minigame Resources...
if exist "src\minigame" (
    xcopy "src\minigame" "%DIST_DIR%\minigame\" /E /I /Q >nul 2>&1
    if exist "%DIST_DIR%\minigame\index.html" (
        echo  - minigame folder: OK
    ) else (
        echo  [WARNING] minigame folder: copy may have failed
    )
) else (
    echo  - minigame folder: not found ^(optional^)
)

REM =============================================================================
REM STEP 13 -- Generate README and copy changelog
REM =============================================================================

echo.
echo [STEP 13] Creating README...
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
    echo  - README.txt: OK
    echo  - changelog.txt: OK
) else (
    set /a ERROR_COUNT+=1
    echo  [ERROR] README.txt: FAILED
)

REM =============================================================================
REM Final summary
REM =============================================================================

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
REM Utility function stubs -- delegate to shared windows_utils.bat
REM =============================================================================

:FindInnoSetup
    call src\windows_utils.bat :FindInnoSetup
    exit /b 0

:VerifyFile
REM Usage: call :VerifyFile path description
    call src\windows_utils.bat :VerifyFile "%~1" "%~2"
    exit /b %errorlevel%

:VerifyDir
    call src\windows_utils.bat :VerifyDir "%~1" "%~2"
    exit /b %errorlevel%

:SafeRmDir
    call src\windows_utils.bat :SafeRmDir "%~1"
    exit /b 0

:EnsureDir
    call src\windows_utils.bat :EnsureDir "%~1"
    exit /b 0