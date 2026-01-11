@echo off
setlocal enabledelayedexpansion

REM Check for silent mode
set "SILENT_MODE=0"
if "%1"=="--silent" set "SILENT_MODE=1"

echo ===========================================
echo  TStar Deployment Script
echo ===========================================

REM Move to project root (parent directory of this script)
pushd "%~dp0.."

REM Detect build directory
if exist "build_win\TStar.exe" (
    set "BUILD_DIR=build_win"
) else if exist "build\TStar.exe" (
    set "BUILD_DIR=build"
) else (
    echo [ERROR] No TStar.exe found in build or build_win.
    echo Please build the project first.
    if !SILENT_MODE!==0 pause
    exit /b 1
)

set "EXE_PATH=!BUILD_DIR!\TStar.exe"
set "MINGW_BIN=C:\Qt\Tools\mingw1310_64\bin"
set "QT_BIN=C:\Qt\6.10.1\mingw_64\bin"
set "WINDEPLOYQT=!QT_BIN!\windeployqt.exe"

echo [INFO] Using build directory: !BUILD_DIR!

if not exist "%EXE_PATH%" (
    echo [ERROR] %EXE_PATH% not found. Please build first.
    if !SILENT_MODE!==0 pause
    exit /b 1
)

echo [STEP 1] Running windeployqt...
"!WINDEPLOYQT!" --release --compiler-runtime "!EXE_PATH!"
if %errorlevel% neq 0 (
    echo [ERROR] windeployqt failed!
    if !SILENT_MODE!==0 pause
    exit /b 1
)

echo [STEP 2] Copying MinGW runtime DLLs...
copy "%MINGW_BIN%\libgcc_s_seh-1.dll" "!BUILD_DIR!\" >nul
copy "%MINGW_BIN%\libstdc++-6.dll" "!BUILD_DIR!\" >nul
copy "%MINGW_BIN%\libwinpthread-1.dll" "!BUILD_DIR!\" >nul
copy "%MINGW_BIN%\libgomp-1.dll" "!BUILD_DIR!\" >nul

echo [STEP 2.1] Copying OpenSSL DLLs...
set "OPENSSL_DIR=%MINGW_BIN%\..\opt\bin"
copy "!OPENSSL_DIR!\libssl-1_1-x64.dll" "!BUILD_DIR!\" >nul
copy "!OPENSSL_DIR!\libcrypto-1_1-x64.dll" "!BUILD_DIR!\" >nul

echo [STEP 3] Copying local dependencies...

REM Copy GSL DLLs
echo   - GSL: OK
copy "deps\gsl\bin\libgsl-28.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\gsl\bin\libgslcblas-0.dll" "!BUILD_DIR!\" >nul 2>&1

REM Copy OpenCV DLLs
echo   - OpenCV: OK
copy "deps\opencv\x64\mingw\bin\libopencv_core*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_imgproc*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_imgcodecs*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_videoio*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_highgui*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_calib3d*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_features2d*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_flann*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_objdetect*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_photo*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_stitching*.dll" "!BUILD_DIR!\" >nul 2>&1
copy "deps\opencv\x64\mingw\bin\libopencv_video*.dll" "!BUILD_DIR!\" >nul 2>&1

REM Copy LibRaw DLL
echo   - LibRaw: OK
copy "deps\libraw\bin\libraw.dll" "!BUILD_DIR!\" >nul 2>&1

echo.
echo [SUCCESS] Dependencies copied to !BUILD_DIR!
echo You can now run TStar.exe from Explorer!
exit /b 0
