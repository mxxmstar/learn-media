<#
.SYNOPSIS
  Setup vcpkg in the project root and install dependencies for src-cpp.

.DESCRIPTION
  1. Clones vcpkg to <project-root>/vcpkg/ (skips if already exists)
  2. Bootstraps vcpkg (builds vcpkg.exe)
  3. Installs packages listed in src-cpp/vcpkg.json for x64-windows
#>

$RootDir = Resolve-Path "$PSScriptRoot\.."
$VcpkgDir = Join-Path $RootDir "vcpkg"
$CppDir   = Join-Path $RootDir "src-cpp"
$VcpkgExe = Join-Path $VcpkgDir "vcpkg.exe"

Write-Host "=== Setup vcpkg for learn-media ===" -ForegroundColor Cyan

# ── Step 1: Clone vcpkg ──
if (Test-Path $VcpkgDir) {
    Write-Host "  [1/3] vcpkg/ already exists, skipping clone" -ForegroundColor DarkGray
} else {
    Write-Host "  [1/3] Cloning vcpkg into $VcpkgDir ..." -ForegroundColor Yellow
    git clone https://github.com/microsoft/vcpkg.git $VcpkgDir
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FAILED: git clone vcpkg" -ForegroundColor Red
        exit 1
    }
    Write-Host "  vcpkg cloned" -ForegroundColor Green
}

# ── Step 2: Bootstrap ──
if (Test-Path $VcpkgExe) {
    Write-Host "  [2/3] vcpkg.exe already exists, skipping bootstrap" -ForegroundColor DarkGray
} else {
    Write-Host "  [2/3] Bootstrapping vcpkg ..." -ForegroundColor Yellow
    $bootstrap = Join-Path $VcpkgDir "bootstrap-vcpkg.bat"
    if (-not (Test-Path $bootstrap)) {
        Write-Host "  FAILED: bootstrap-vcpkg.bat not found" -ForegroundColor Red
        exit 1
    }
    & $bootstrap
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FAILED: vcpkg bootstrap" -ForegroundColor Red
        exit 1
    }
    Write-Host "  vcpkg bootstrapped" -ForegroundColor Green
}

# ── Step 3: Install dependencies ──
$manifest = Join-Path $CppDir "vcpkg.json"
if (-not (Test-Path $manifest)) {
    Write-Host "  FAILED: $manifest not found" -ForegroundColor Red
    exit 1
}

Write-Host "  [3/3] Installing dependencies from src-cpp/vcpkg.json ..." -ForegroundColor Yellow
& $VcpkgExe install "--x-manifest-root=$CppDir" --triplet=x64-windows
if ($LASTEXITCODE -ne 0) {
    Write-Host "  FAILED: vcpkg install" -ForegroundColor Red
    exit 1
}

Write-Host "`n=== Setup complete ===" -ForegroundColor Green
Write-Host "Run 'script\build-src-cpp.ps1' to build." -ForegroundColor Cyan
