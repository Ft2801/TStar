
$ErrorActionPreference = "Stop"

$PYTHON_VER = "3.11.9"
$PYTHON_URL = "https://www.python.org/ftp/python/$PYTHON_VER/python-$PYTHON_VER-embed-amd64.zip"
$PIP_URL = "https://bootstrap.pypa.io/get-pip.py"

$DEPS_DIR = Join-Path $PSScriptRoot "deps"
$PYTHON_DIR = Join-Path $DEPS_DIR "python"
$PthFile = Join-Path $PYTHON_DIR "python311._pth"

Write-Host ">>> TStar Python Environment Setup <<<"
Write-Host "Target Directory: $PYTHON_DIR"

# 1. Prepare Directory
if (!(Test-Path $DEPS_DIR)) { New-Item -ItemType Directory -Path $DEPS_DIR | Out-Null }
if (Test-Path $PYTHON_DIR) {
    Write-Host "Cleaning existing python dir..."
    Remove-Item -Path $PYTHON_DIR -Recurse -Force
}
New-Item -ItemType Directory -Path $PYTHON_DIR | Out-Null

# 2. Download Python Embeddable
$ZipPath = Join-Path $DEPS_DIR "python.zip"
if (!(Test-Path $ZipPath)) {
    Write-Host "Downloading Python $PYTHON_VER..."
    Invoke-WebRequest -Uri $PYTHON_URL -OutFile $ZipPath
} else {
    Write-Host "Python zip found, using cache."
}

# 3. Extract
Write-Host "Extracting Python..."
Expand-Archive -Path $ZipPath -DestinationPath $PYTHON_DIR -Force

# 4. Configure ._pth to allow site-packages (Enable 'import site')
# Default _pth comments out 'import site', we need it for pip
Write-Host "Configuring python311._pth..."
$content = Get-Content $PthFile
$content = $content -replace "#import site", "import site"
$content | Set-Content $PthFile

# 5. Install Pip
Write-Host "Installing pip..."
$GetPip = Join-Path $PYTHON_DIR "get-pip.py"
Invoke-WebRequest -Uri $PIP_URL -OutFile $GetPip

$PythonExe = Join-Path $PYTHON_DIR "python.exe"
& $PythonExe $GetPip --no-warn-script-location

# 6. Install Dependencies
Write-Host "Installing Libraries: numpy, tifffile, imagecodecs, astropy, onnxruntime..."
& $PythonExe -m pip install numpy tifffile imagecodecs astropy onnxruntime --no-warn-script-location

# Cleanup
Remove-Item $GetPip -Force

Write-Host ">>> Python Setup Complete! <<<"
Write-Host "You can now run package_dist.bat"
