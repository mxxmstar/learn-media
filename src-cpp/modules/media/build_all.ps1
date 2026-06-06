param(
    [ValidateSet('build', 'rebuild', 'clean', 'help')]
    [string]$Action = 'build'
)

if ($Action -eq 'help') {
    Write-Host @"
Usage: .\build_all.ps1 [<action>]

Actions:
  build     Compile (default). Will auto-configure if needed.
  rebuild   Clean build directory then recompile from scratch.
  clean     Remove compiled artifacts (keeps build cache).
  help      Show this help message.

Examples:
  .\build_all.ps1          -> build
  .\build_all.ps1 rebuild  -> clean rebuild
  .\build_all.ps1 clean    -> clean
"@
    exit 0
}

$ini = Join-Path $PSScriptRoot "..\..\script\path.ini"
$iniDir = Split-Path $ini -Parent

$vsDir = (Select-String -Path $ini -Pattern "^VS_INSTALL_DIR=(.+)").Matches.Groups[1].Value.Trim()
$vcpkgDir = (Select-String -Path $ini -Pattern "^VCPKG_DIR=(.+)").Matches.Groups[1].Value.Trim()

if (-not [System.IO.Path]::IsPathRooted($vsDir)) {
    $vsDir = Join-Path $iniDir $vsDir
}
if (-not [System.IO.Path]::IsPathRooted($vcpkgDir)) {
    $vcpkgDir = Join-Path $iniDir $vcpkgDir
}

if (-not $vsDir -or -not $vcpkgDir) {
    Write-Host "[ERROR] VS_INSTALL_DIR 或 VCPKG_DIR 为空，请先填写 path.ini" -ForegroundColor Red
    exit 1
}

$manifestDir = Join-Path $PSScriptRoot "..\.."
$manifestDir = Resolve-Path $manifestDir

$vsDevCmd = Join-Path $vsDir "Common7\Tools\VsDevCmd.bat"
$vcpkgTc = Join-Path $vcpkgDir "scripts\buildsystems\vcpkg.cmake"
$buildDir = Join-Path $PSScriptRoot "build"
$binDir = Join-Path $PSScriptRoot "bin"
$libDir = Join-Path $PSScriptRoot "lib"

if (-not (Test-Path $vsDevCmd)) {
    Write-Host "[ERROR] VsDevCmd.bat 不存在: $vsDevCmd" -ForegroundColor Red
    exit 1
}

Write-Host "=== media (all submodules) ===" -ForegroundColor Cyan
Write-Host "action: $Action" -ForegroundColor Gray
Write-Host "VS    : $vsDir" -ForegroundColor DarkGray
Write-Host "VCPKG : $vcpkgDir" -ForegroundColor DarkGray
Write-Host ""

if ($Action -eq 'clean') {
    Write-Host ">>> cmake --build --target clean ..." -ForegroundColor Yellow
    cmd /c """$vsDevCmd"" -arch=x64 -host_arch=x64 && cmake --build ""$buildDir"" --target clean"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "=== Done ===" -ForegroundColor Green
    } else {
        Write-Host "=== Failed (exit: $LASTEXITCODE) ===" -ForegroundColor Red
    }
    exit
}

if ($Action -eq 'rebuild') {
    Write-Host ">>> Removing previous build directory ..." -ForegroundColor Yellow
    if (Test-Path $buildDir) {
        Remove-Item -Recurse -Force $buildDir
    }
    Write-Host ">>> configure + build (clean) ..." -ForegroundColor Yellow
} else {
    Write-Host ">>> configure + build ..." -ForegroundColor Yellow
}

cmd /c """$vsDevCmd"" -arch=x64 -host_arch=x64 && cmake -S . -B ""$buildDir"" -DVCPKG_MANIFEST_DIR=""$manifestDir"" -DVCPKG_MANIFEST_MODE=ON -DCMAKE_TOOLCHAIN_FILE=""$vcpkgTc"" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=""$binDir"" -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=""$libDir"" -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=""$libDir"" && cmake --build ""$buildDir"""

if ($LASTEXITCODE -eq 0) {
    Write-Host "=== Done ===" -ForegroundColor Green
} else {
    Write-Host "=== Failed (exit: $LASTEXITCODE) ===" -ForegroundColor Red
}
