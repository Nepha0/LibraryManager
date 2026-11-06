param(
    [string]$QtKit = "E:\Qt\6.12.0\mingw_64",
    [string]$MingwBin = "E:\Qt\Tools\mingw1310_64\bin",
    [switch]$Run
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$workspaceRoot = Split-Path -Parent $projectRoot
$buildRoot = Join-Path $workspaceRoot "build-LibraryManager"
$projectFile = Join-Path $projectRoot "LibraryManager.pro"

if (-not (Test-Path (Join-Path $QtKit "bin\qmake.exe"))) {
    throw "qmake not found under $QtKit"
}
if (-not (Test-Path (Join-Path $MingwBin "mingw32-make.exe"))) {
    throw "mingw32-make not found under $MingwBin"
}

New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$env:Path = "$(Join-Path $QtKit 'bin');$MingwBin;$env:Path"

Push-Location $buildRoot
try {
    & (Join-Path $QtKit "bin\qmake.exe") $projectFile
    if ($LASTEXITCODE -ne 0) {
        throw "qmake failed"
    }
    & (Join-Path $MingwBin "mingw32-make.exe") -j4
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed"
    }

    $executable = Join-Path $buildRoot "release\LibraryManager.exe"
    & (Join-Path $QtKit "bin\windeployqt.exe") --release --no-translations $executable
    if ($LASTEXITCODE -ne 0) {
        throw "windeployqt failed"
    }

    Write-Host "Demo ready: $executable"
    if ($Run) {
        Start-Process -FilePath $executable
    }
}
finally {
    Pop-Location
}
