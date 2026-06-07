param(
    [Parameter(Mandatory = $true)]
    [string]$ModuleName,

    [Parameter(Mandatory = $true)]
    [string]$SourceDir,

    [ValidateSet('build', 'rebuild', 'clean', 'clean-cache', 'test', 'help')]
    [string]$Action = 'build',

    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Debug',

    [switch]$RunTests,
    [switch]$InstallDeps
)

$ErrorActionPreference = 'Stop'

function Get-IniValue {
    param(
        [string]$Path,
        [string]$Name
    )

    $match = Select-String -Path $Path -Pattern "^\s*$([regex]::Escape($Name))\s*=\s*(.+?)\s*$" | Select-Object -First 1
    if ($match) {
        return $match.Matches[0].Groups[1].Value.Trim()
    }
    return $null
}

function Resolve-ConfigPath {
    param(
        [string]$BaseDir,
        [string]$Path
    )

    if ([string]::IsNullOrWhiteSpace($Path)) {
        return $null
    }

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return Join-Path $BaseDir $Path
}

function Get-CMakeBool {
    param([bool]$Value)

    if ($Value) { return 'ON' }
    return 'OFF'
}

function Get-CMakeCacheValue {
    param(
        [string]$BuildDir,
        [string]$Name
    )

    $cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cacheFile)) {
        return $null
    }

    foreach ($line in (Get-Content -LiteralPath $cacheFile)) {
        if ($line -match "^$([regex]::Escape($Name))(?::[^=]*)?=(.*)$") {
            return $Matches[1]
        }
    }

    return $null
}

function Assert-SafeBuildDir {
    param(
        [string]$SourceDir,
        [string]$BuildDir
    )

    $sourceFull = [System.IO.Path]::GetFullPath($SourceDir).TrimEnd([char[]]@('\', '/'))
    $buildFull = [System.IO.Path]::GetFullPath($BuildDir)
    $prefix = $sourceFull + [System.IO.Path]::DirectorySeparatorChar

    if ((Split-Path -Leaf $buildFull) -ne 'build') {
        throw "Refusing to remove '$buildFull' because the directory name is not 'build'."
    }

    if (-not $buildFull.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove '$buildFull' because it is outside '$sourceFull'."
    }
}

function Remove-BuildDir {
    param(
        [string]$SourceDir,
        [string]$BuildDir
    )

    Assert-SafeBuildDir -SourceDir $SourceDir -BuildDir $BuildDir
    if (Test-Path -LiteralPath $BuildDir) {
        Write-Host ">>> Removing build directory: $BuildDir" -ForegroundColor Yellow
        Remove-Item -LiteralPath $BuildDir -Recurse -Force
    }
}

function Reset-BuildDirIfCacheChanged {
    param(
        [string]$SourceDir,
        [string]$BuildDir,
        [string]$DesiredManifestMode
    )

    $cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cacheFile)) {
        return
    }

    $reasons = @()
    $generator = Get-CMakeCacheValue -BuildDir $BuildDir -Name 'CMAKE_GENERATOR'
    if ($generator -and $generator -ne 'NMake Makefiles') {
        $reasons += "generator changed from '$generator' to 'NMake Makefiles'"
    }

    foreach ($entry in @('VCPKG_MANIFEST_MODE', 'Z_VCPKG_CHECK_MANIFEST_MODE')) {
        $value = Get-CMakeCacheValue -BuildDir $BuildDir -Name $entry
        if ($value -and $value.ToUpperInvariant() -ne $DesiredManifestMode) {
            $reasons += "$entry changed from '$value' to '$DesiredManifestMode'"
        }
    }

    if ($reasons.Count -gt 0) {
        Write-Host ">>> CMake cache is incompatible; reconfiguring from a clean build dir" -ForegroundColor Yellow
        foreach ($reason in $reasons) {
            Write-Host "    - $reason" -ForegroundColor DarkGray
        }
        Remove-BuildDir -SourceDir $SourceDir -BuildDir $BuildDir
    }
}

function Invoke-DeveloperCommand {
    param(
        [string]$VsDevCmd,
        [string]$Step,
        [string]$Command
    )

    Write-Host ">>> $Step" -ForegroundColor Yellow
    $devCommand = "`"$VsDevCmd`" -arch=x64 -host_arch=x64 > nul 2>&1 && $Command"
    cmd /c $devCommand
    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE."
    }
}

function Get-TestOptions {
    param([string]$ModuleName)

    $options = @('BUILD_TESTS')
    switch ($ModuleName.ToLowerInvariant()) {
        'media/defines' { $options += 'BUILD_MEDIA_TESTS' }
        'media/puller'  { $options += 'BUILD_PULLER_TESTS' }
        'media/decoder' { $options += 'BUILD_DECODER_TESTS' }
        'media/encoder' { $options += 'BUILD_ENCODER_TESTS' }
        'media/pusher'  { $options += 'BUILD_MEDIA_PUSHER_TESTS' }
        'media/stream'  { $options += 'BUILD_STREAM_TESTS' }
    }

    return $options
}

if ($Action -eq 'help') {
    Write-Host @"
Usage: .\build.ps1 [<action>] [-Config <config>] [-RunTests] [-InstallDeps]

Actions:
  build        Configure and compile (default).
  rebuild      Remove build directory, then configure and compile.
  clean        Run CMake clean in the existing build directory.
  clean-cache  Remove the build directory.
  test         Configure with tests, compile, then run ctest.
  help         Show this help message.

Examples:
  .\build.ps1
  .\build.ps1 rebuild
  .\build.ps1 test
  .\build.ps1 build -Config Release
  .\build.ps1 build -InstallDeps
"@
    exit 0
}

$SourceDir = (Resolve-Path -LiteralPath $SourceDir).Path
$ScriptDir = Split-Path -Parent $PSCommandPath
$CppRoot = (Resolve-Path -LiteralPath (Join-Path $ScriptDir '..')).Path
$PathIni = Join-Path $ScriptDir 'path.ini'

if (-not (Test-Path -LiteralPath $PathIni)) {
    throw "path.ini not found: $PathIni"
}

$iniDir = Split-Path -Parent $PathIni
$vsDir = Resolve-ConfigPath -BaseDir $iniDir -Path (Get-IniValue -Path $PathIni -Name 'VS_INSTALL_DIR')
$vcpkgDir = Resolve-ConfigPath -BaseDir $iniDir -Path (Get-IniValue -Path $PathIni -Name 'VCPKG_DIR')
$triplet = Get-IniValue -Path $PathIni -Name 'VCPKG_TRIPLET'
if ([string]::IsNullOrWhiteSpace($triplet)) {
    $triplet = 'x64-windows'
}

if (-not $vsDir -or -not (Test-Path -LiteralPath $vsDir)) {
    throw "VS_INSTALL_DIR is invalid: $vsDir"
}
if (-not $vcpkgDir -or -not (Test-Path -LiteralPath $vcpkgDir)) {
    throw "VCPKG_DIR is invalid: $vcpkgDir"
}

$vsDevCmd = Join-Path $vsDir 'Common7\Tools\VsDevCmd.bat'
$vcpkgToolchain = Join-Path $vcpkgDir 'scripts\buildsystems\vcpkg.cmake'
if (-not (Test-Path -LiteralPath $vsDevCmd)) {
    throw "VsDevCmd.bat not found: $vsDevCmd"
}
if (-not (Test-Path -LiteralPath $vcpkgToolchain)) {
    throw "vcpkg toolchain not found: $vcpkgToolchain"
}

$buildDir = Join-Path $SourceDir 'build'
$binDir = Join-Path $SourceDir 'bin'
$libDir = Join-Path $SourceDir 'lib'
$installedDir = Join-Path $CppRoot 'vcpkg_installed'
$tripletDir = Join-Path $installedDir $triplet
$runTestsValue = [bool]($RunTests -or $Action -eq 'test')
$testsValue = Get-CMakeBool $runTestsValue
$manifestMode = if ($InstallDeps) { 'ON' } else { 'OFF' }

Write-Host "=== $ModuleName ===" -ForegroundColor Cyan
Write-Host "action: $Action" -ForegroundColor Gray
Write-Host "config: $Config" -ForegroundColor Gray
Write-Host "tests : $testsValue" -ForegroundColor Gray
Write-Host "VS    : $vsDir" -ForegroundColor DarkGray
Write-Host "vcpkg : $vcpkgDir" -ForegroundColor DarkGray
Write-Host "triplet: $triplet" -ForegroundColor DarkGray
if (-not $InstallDeps) {
    Write-Host "vcpkg install: disabled (use -InstallDeps to run manifest install)" -ForegroundColor DarkGray
}
Write-Host ""

if ($Action -eq 'clean-cache') {
    Remove-BuildDir -SourceDir $SourceDir -BuildDir $buildDir
    Write-Host "=== Done ===" -ForegroundColor Green
    exit 0
}

if ($Action -eq 'clean') {
    if (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt')) {
        Invoke-DeveloperCommand -VsDevCmd $vsDevCmd -Step 'cmake clean' -Command "cmake --build `"$buildDir`" --target clean --config $Config"
    } else {
        Write-Host ">>> build directory is not configured; nothing to clean" -ForegroundColor DarkGray
    }
    Write-Host "=== Done ===" -ForegroundColor Green
    exit 0
}

if ($Action -eq 'rebuild') {
    Remove-BuildDir -SourceDir $SourceDir -BuildDir $buildDir
}

Reset-BuildDirIfCacheChanged -SourceDir $SourceDir -BuildDir $buildDir -DesiredManifestMode $manifestMode

if (-not $InstallDeps -and -not (Test-Path -LiteralPath $tripletDir)) {
    Write-Host "WARNING: $tripletDir does not exist. Run with -InstallDeps or setup vcpkg first." -ForegroundColor Yellow
}

$cmakeArgs = "-S `"$SourceDir`" -B `"$buildDir`" -G `"NMake Makefiles`""
$cmakeArgs += " -DCMAKE_BUILD_TYPE=$Config"
$cmakeArgs += " -DCMAKE_TOOLCHAIN_FILE=`"$vcpkgToolchain`""
$cmakeArgs += " -DVCPKG_MANIFEST_DIR=`"$CppRoot`""
$cmakeArgs += " -DVCPKG_INSTALLED_DIR=`"$installedDir`""
$cmakeArgs += " -DVCPKG_TARGET_TRIPLET=$triplet"
$cmakeArgs += " -DVCPKG_MANIFEST_MODE=$manifestMode"
$cmakeArgs += " -DCMAKE_PREFIX_PATH=`"$tripletDir`""
$cmakeArgs += " -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=`"$binDir`""
$cmakeArgs += " -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=`"$libDir`""
$cmakeArgs += " -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=`"$libDir`""

foreach ($option in (Get-TestOptions -ModuleName $ModuleName)) {
    $cmakeArgs += " -D$option=$testsValue"
}

Invoke-DeveloperCommand -VsDevCmd $vsDevCmd -Step 'cmake configure' -Command "cmake --no-warn-unused-cli $cmakeArgs"
Invoke-DeveloperCommand -VsDevCmd $vsDevCmd -Step 'cmake build' -Command "cmake --build `"$buildDir`" --config $Config"

if ($runTestsValue) {
    Invoke-DeveloperCommand -VsDevCmd $vsDevCmd -Step 'ctest' -Command "ctest --test-dir `"$buildDir`" -C $Config --output-on-failure"
}

Write-Host "=== Done ===" -ForegroundColor Green
