<#
.SYNOPSIS
  Build, clean, rebuild, or list src-cpp CMake targets.

.EXAMPLE
  .\script\build-src-cpp.ps1

.EXAMPLE
  .\script\build-src-cpp.ps1 -Target common -Config Release

.EXAMPLE
  .\script\build-src-cpp.ps1 -Target media/defines -RunTests

.EXAMPLE
  .\script\build-src-cpp.ps1 -Target common -InstallDeps

.EXAMPLE
  .\script\build-src-cpp.ps1 -Action clean -Target modules

.EXAMPLE
  .\script\build-src-cpp.ps1 -Action rebuild -Target all

.EXAMPLE
  .\script\build-src-cpp.ps1 -Action list
#>

[CmdletBinding()]
param(
    [ValidateSet("build", "clean", "rebuild", "list")]
    [string]$Action = "build",

    [Alias("Module")]
    [string[]]$Target = @("all"),

    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",

    [switch]$RunTests,
    [switch]$Standalone,
    [switch]$InstallDeps
)

$ErrorActionPreference = "Stop"

$ScriptDir  = Split-Path -Parent $PSCommandPath
$RootDir    = (Resolve-Path -LiteralPath (Join-Path $ScriptDir "..")).Path
$CppDir     = Join-Path $RootDir "src-cpp"
$ModulesDir = Join-Path $CppDir "modules"
$ConfigFile = Join-Path $ScriptDir "build-env.json"

$script:BuildEnvironmentReady = $false
$script:VcVarsAll = $null
$script:VcVarsArg = "x64"
$script:Toolchain = $null
$script:VsPath = $null
$script:VcpkgTriplet = "x64-windows"

function Get-CMakeBool {
    param([bool]$Value)

    if ($Value) { return "ON" }
    return "OFF"
}

function Get-TestOptions {
    param([string]$TargetName)

    $options = @("BUILD_TESTS")

    switch -Wildcard ($TargetName) {
        "all" {
            $options += @("BUILD_MEDIA_TESTS", "BUILD_PULLER_TESTS", "BUILD_DECODER_TESTS")
            break
        }
        "media" {
            $options += @("BUILD_MEDIA_TESTS", "BUILD_PULLER_TESTS", "BUILD_DECODER_TESTS")
            break
        }
        "media/defines" {
            $options += "BUILD_MEDIA_TESTS"
            break
        }
        "media/puller" {
            $options += "BUILD_PULLER_TESTS"
            break
        }
        "media/decoder" {
            $options += "BUILD_DECODER_TESTS"
            break
        }
    }

    return @($options)
}

function Get-RelativePath {
    param(
        [string]$BasePath,
        [string]$ChildPath
    )

    $base = (Resolve-Path -LiteralPath $BasePath).Path.TrimEnd([char[]]@('\', '/'))
    $child = (Resolve-Path -LiteralPath $ChildPath).Path
    $prefix = $base + [System.IO.Path]::DirectorySeparatorChar

    if ($child.Equals($base, [System.StringComparison]::OrdinalIgnoreCase)) {
        return ""
    }

    if ($child.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $child.Substring($prefix.Length)
    }

    return $child
}

function Get-TargetKey {
    param([string]$Value)

    $key = $Value.Trim().Trim('"').Trim("'") -replace "\\", "/"
    $key = $key.Trim([char[]]@('/'))

    foreach ($prefix in @("src-cpp/modules/", "src-cpp/", "modules/")) {
        if ($key.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            $key = $key.Substring($prefix.Length)
            break
        }
    }

    if ([string]::IsNullOrWhiteSpace($key) -or $key -eq ".") {
        return "all"
    }

    return $key.ToLowerInvariant()
}

function Get-ModuleInfos {
    if (-not (Test-Path -LiteralPath $ModulesDir)) {
        return @()
    }

    $cmakeFiles = Get-ChildItem -LiteralPath $ModulesDir -Recurse -Filter "CMakeLists.txt" -File
    $modules = foreach ($file in $cmakeFiles) {
        $dir = $file.Directory.FullName
        $relative = Get-RelativePath $ModulesDir $dir
        $pathSegments = @($relative -split "[\\/]")
        if ($pathSegments -contains "build" -or $pathSegments -contains "test") {
            continue
        }

        $name = ($relative -replace "\\", "/").ToLowerInvariant()
        $segments = @($name -split "/")

        [pscustomobject]@{
            Name     = $name
            Kind     = "module"
            Path     = $dir
            BuildDir = Join-Path $dir "build"
            Depth    = $segments.Count
        }
    }

    return @($modules | Sort-Object @{ Expression = "Depth"; Ascending = $true }, @{ Expression = "Name"; Ascending = $true })
}

function Select-UniqueTargets {
    param([object[]]$Items)

    $seen = @{}
    $result = @()
    foreach ($item in $Items) {
        $key = $item.Path.ToLowerInvariant()
        if (-not $seen.ContainsKey($key)) {
            $seen[$key] = $true
            $result += $item
        }
    }

    return @($result)
}

$RootTarget = [pscustomobject]@{
    Name     = "all"
    Kind     = "root"
    Path     = $CppDir
    BuildDir = Join-Path $CppDir "build"
    Depth    = 0
}

$ModuleInfos = @(Get-ModuleInfos)
$TargetMap = @{}
$AliasMap = @{}

foreach ($alias in @("all", "root", "src-cpp")) {
    $TargetMap[$alias] = $RootTarget
}

foreach ($module in $ModuleInfos) {
    $TargetMap[$module.Name] = $module

    $leaf = @($module.Name -split "/")[-1]
    if (-not $AliasMap.ContainsKey($leaf)) {
        $AliasMap[$leaf] = @()
    }
    $AliasMap[$leaf] += $module
}

function Show-Targets {
    Write-Host ""
    Write-Host "Available targets:" -ForegroundColor Cyan
    Write-Host "  all        src-cpp root project"
    Write-Host "  modules    every module with a CMakeLists.txt under src-cpp/modules"

    foreach ($module in $ModuleInfos) {
        Write-Host ("  {0,-10} {1}" -f $module.Name, $module.Path)
    }

    $uniqueAliases = @()
    foreach ($key in $AliasMap.Keys) {
        $matches = @($AliasMap[$key])
        if ($matches.Count -eq 1 -and $matches[0].Name -ne $key) {
            $uniqueAliases += ("{0} -> {1}" -f $key, $matches[0].Name)
        }
    }

    if ($uniqueAliases.Count -gt 0) {
        Write-Host ""
        Write-Host "Unique aliases:" -ForegroundColor Cyan
        foreach ($alias in ($uniqueAliases | Sort-Object)) {
            Write-Host "  $alias"
        }
    }

    Write-Host ""
    Write-Host "Examples:" -ForegroundColor Cyan
    Write-Host "  .\script\build-src-cpp.ps1 -Target common"
    Write-Host "  .\script\build-src-cpp.ps1 -Target common -InstallDeps"
    Write-Host "  .\script\build-src-cpp.ps1 -Target media/defines -RunTests"
    Write-Host "  .\script\build-src-cpp.ps1 -Action clean -Target modules"
    Write-Host "  .\script\build-src-cpp.ps1 -Action rebuild -Target all -Config Release"
}

function Resolve-PathTarget {
    param([string]$RawTarget)

    $candidate = $RawTarget.Trim().Trim('"').Trim("'")
    if ([string]::IsNullOrWhiteSpace($candidate)) {
        return @()
    }

    if (-not [System.IO.Path]::IsPathRooted($candidate)) {
        $candidate = Join-Path $RootDir $candidate
    }

    if (-not (Test-Path -LiteralPath $candidate)) {
        return @()
    }

    $candidate = (Resolve-Path -LiteralPath $candidate).Path
    $cmakeFile = Join-Path $candidate "CMakeLists.txt"
    if (-not (Test-Path -LiteralPath $cmakeFile)) {
        throw "Target path '$RawTarget' exists, but it does not contain a CMakeLists.txt."
    }

    $cppRoot = (Resolve-Path -LiteralPath $CppDir).Path.TrimEnd([char[]]@('\', '/'))
    if ($candidate.Equals($cppRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        return @($RootTarget)
    }

    $prefix = $cppRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Target path '$RawTarget' is outside src-cpp."
    }

    $name = (Get-RelativePath $ModulesDir $candidate) -replace "\\", "/"
    $name = $name.ToLowerInvariant()
    return @([pscustomobject]@{
        Name     = $name
        Kind     = "module"
        Path     = $candidate
        BuildDir = Join-Path $candidate "build"
        Depth    = @($name -split "/").Count
    })
}

function Resolve-Target {
    param([string]$RawTarget)

    $key = Get-TargetKey $RawTarget

    if ($key -eq "modules" -or $key -eq "module") {
        if ($ModuleInfos.Count -eq 0) {
            throw "No modules were found under $ModulesDir."
        }
        return @($ModuleInfos)
    }

    if ($TargetMap.ContainsKey($key)) {
        return @($TargetMap[$key])
    }

    if ($AliasMap.ContainsKey($key)) {
        $matches = @($AliasMap[$key])
        if ($matches.Count -eq 1) {
            return @($matches[0])
        }

        $names = ($matches | ForEach-Object { $_.Name }) -join ", "
        throw "Target '$RawTarget' is ambiguous. Use one of: $names."
    }

    $pathTargets = @(Resolve-PathTarget $RawTarget)
    if ($pathTargets.Count -gt 0) {
        return @($pathTargets)
    }

    throw "Unknown target '$RawTarget'. Run '.\script\build-src-cpp.ps1 -Action list' to see available targets."
}

function Resolve-Targets {
    param([string[]]$RawTargets)

    $resolved = @()
    foreach ($raw in $RawTargets) {
        if ([string]::IsNullOrWhiteSpace($raw)) { continue }
        $resolved += @(Resolve-Target $raw)
    }

    $resolved = @(Select-UniqueTargets $resolved)
    if ($resolved.Count -eq 0) {
        throw "No build targets were selected."
    }

    return @($resolved)
}

function Initialize-BuildEnvironment {
    if ($script:BuildEnvironmentReady) {
        return
    }

    if (-not (Test-Path -LiteralPath $ConfigFile)) {
        throw "$ConfigFile not found."
    }

    $cfg = Get-Content -LiteralPath $ConfigFile -Raw | ConvertFrom-Json
    $script:VsPath = $cfg.vs_path
    if ([string]::IsNullOrWhiteSpace($script:VsPath) -or -not (Test-Path -LiteralPath $script:VsPath)) {
        throw "vs_path '$($cfg.vs_path)' was not found."
    }

    $script:VcVarsAll = Join-Path $script:VsPath "VC\Auxiliary\Build\vcvarsall.bat"
    if (-not (Test-Path -LiteralPath $script:VcVarsAll)) {
        throw "vcvarsall.bat was not found under '$($script:VsPath)'."
    }

    $script:VcVarsArg = "x64"
    if ($cfg.msvc_toolset) {
        $script:VcVarsArg += " -vcvars_ver=$($cfg.msvc_toolset)"
    }
    if ($cfg.vcpkg_triplet) {
        $script:VcpkgTriplet = $cfg.vcpkg_triplet
    }

    $script:Toolchain = $null
    foreach ($path in @((Join-Path $RootDir "vcpkg"), (Join-Path $CppDir "vcpkg"), $env:VCPKG_ROOT)) {
        if ([string]::IsNullOrWhiteSpace($path)) { continue }

        $toolchain = Join-Path $path "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path -LiteralPath $toolchain) {
            $script:Toolchain = $toolchain
            break
        }
    }

    Write-Host "Visual Studio: $($script:VsPath)" -ForegroundColor DarkGray
    if ($cfg.msvc_toolset) {
        Write-Host "Toolset: $($cfg.msvc_toolset)" -ForegroundColor DarkGray
    }
    if ($script:Toolchain) {
        Write-Host "vcpkg: $($script:Toolchain)" -ForegroundColor DarkGray
        Write-Host "vcpkg triplet: $($script:VcpkgTriplet)" -ForegroundColor DarkGray
        if (-not $InstallDeps) {
            Write-Host "vcpkg install: disabled (use -InstallDeps to run manifest install)" -ForegroundColor DarkGray
        }
    }

    $script:BuildEnvironmentReady = $true
}

function Invoke-DeveloperCmd {
    param(
        [string]$Step,
        [string]$Command
    )

    Write-Host ">> $Step" -ForegroundColor DarkGray
    $developerCmd = "`"$($script:VcVarsAll)`" $($script:VcVarsArg) > nul 2>&1 && $Command"
    cmd /c $developerCmd

    if ($LASTEXITCODE -ne 0) {
        throw "$Step failed with exit code $LASTEXITCODE."
    }
}

function Get-SafeBuildDir {
    param([object]$TargetInfo)

    $buildDir = [System.IO.Path]::GetFullPath($TargetInfo.BuildDir)
    $cppRoot = [System.IO.Path]::GetFullPath($CppDir).TrimEnd([char[]]@('\', '/'))
    $prefix = $cppRoot + [System.IO.Path]::DirectorySeparatorChar
    $leaf = Split-Path -Leaf $buildDir

    if ($leaf -ne "build") {
        throw "Refusing to clean '$buildDir' because the directory name is not 'build'."
    }

    if (-not $buildDir.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean '$buildDir' because it is outside src-cpp."
    }

    return $buildDir
}

function Invoke-Clean {
    param([object]$TargetInfo)

    $buildDir = Get-SafeBuildDir $TargetInfo
    if (Test-Path -LiteralPath $buildDir) {
        Write-Host ("`n=== Cleaning {0}: {1} ===" -f $TargetInfo.Name, $buildDir) -ForegroundColor Yellow
        Remove-Item -LiteralPath $buildDir -Recurse -Force
        Write-Host ("=== {0} clean OK ===" -f $TargetInfo.Name) -ForegroundColor Green
    } else {
        Write-Host ("`n=== {0} clean skipped; build directory does not exist ===" -f $TargetInfo.Name) -ForegroundColor DarkGray
    }
}

function Get-CMakeCacheValue {
    param(
        [string]$BuildDir,
        [string]$Name
    )

    $cacheFile = Join-Path $BuildDir "CMakeCache.txt"
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

function Reset-BuildDirIfConfigureCacheChanged {
    param(
        [object]$TargetInfo,
        [string]$DesiredManifestMode
    )

    $buildDir = Get-SafeBuildDir $TargetInfo
    $cacheFile = Join-Path $buildDir "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cacheFile)) {
        return
    }

    $reasons = @()
    $generator = Get-CMakeCacheValue $buildDir "CMAKE_GENERATOR"
    if ($generator -and $generator -ne "NMake Makefiles") {
        $reasons += "generator changed from '$generator' to 'NMake Makefiles'"
    }

    foreach ($entry in @("VCPKG_MANIFEST_MODE", "Z_VCPKG_CHECK_MANIFEST_MODE")) {
        $value = Get-CMakeCacheValue $buildDir $entry
        if ($value -and $value.ToUpperInvariant() -ne $DesiredManifestMode) {
            $reasons += "$entry changed from '$value' to '$DesiredManifestMode'"
        }
    }

    if ($reasons.Count -gt 0) {
        Write-Host ("`n=== Reconfigure cache changed for {0}; cleaning build dir ===" -f $TargetInfo.Name) -ForegroundColor Yellow
        foreach ($reason in $reasons) {
            Write-Host "  - $reason" -ForegroundColor DarkGray
        }
        Invoke-Clean $TargetInfo
    }
}

function Invoke-Build {
    param([object]$TargetInfo)

    $srcDir = $TargetInfo.Path
    $buildDir = $TargetInfo.BuildDir
    $cmakeFile = Join-Path $srcDir "CMakeLists.txt"

    if (-not (Test-Path -LiteralPath $cmakeFile)) {
        throw "$($TargetInfo.Name) does not contain a CMakeLists.txt."
    }

    Write-Host ("`n=== Building {0} ({1}) ===" -f $TargetInfo.Name, $Config) -ForegroundColor Cyan

    $testsValue = Get-CMakeBool ([bool]$RunTests)
    $desiredManifestMode = if ($InstallDeps) { "ON" } else { "OFF" }
    $cmakeArgs = "-S `"$srcDir`" -B `"$buildDir`" -G `"NMake Makefiles`""
    $cmakeArgs += " -DCMAKE_BUILD_TYPE=$Config"

    foreach ($option in (Get-TestOptions $TargetInfo.Name)) {
        $cmakeArgs += " -D$option=$testsValue"
    }

    if ($script:Toolchain) {
        $cmakeArgs += " -DCMAKE_TOOLCHAIN_FILE=`"$($script:Toolchain)`""

        $manifestFile = Join-Path $CppDir "vcpkg.json"
        if (Test-Path -LiteralPath $manifestFile) {
            $installedDir = Join-Path $CppDir "vcpkg_installed"
            $tripletDir = Join-Path $installedDir $script:VcpkgTriplet
            $cmakeArgs += " -DVCPKG_MANIFEST_DIR=`"$CppDir`""
            $cmakeArgs += " -DVCPKG_INSTALLED_DIR=`"$installedDir`""
            $cmakeArgs += " -DVCPKG_TARGET_TRIPLET=$($script:VcpkgTriplet)"
            $cmakeArgs += " -DCMAKE_PREFIX_PATH=`"$tripletDir`""

            if ($InstallDeps) {
                $cmakeArgs += " -DVCPKG_MANIFEST_MODE=$desiredManifestMode"
            } else {
                $cmakeArgs += " -DVCPKG_MANIFEST_MODE=$desiredManifestMode"
                if (-not (Test-Path -LiteralPath $tripletDir)) {
                    Write-Host "WARNING: $tripletDir does not exist. Run script\setup-vcpkg.ps1 or add -InstallDeps." -ForegroundColor Yellow
                }
            }
        }
    }

    if ($Standalone) {
        $cmakeArgs += " -DBUILD_STANDALONE=ON"
    }

    Reset-BuildDirIfConfigureCacheChanged -TargetInfo $TargetInfo -DesiredManifestMode $desiredManifestMode
    Invoke-DeveloperCmd -Step "cmake configure $($TargetInfo.Name)" -Command "cmake $cmakeArgs"
    Invoke-DeveloperCmd -Step "cmake build $($TargetInfo.Name)" -Command "cmake --build `"$buildDir`" --config $Config"

    if ($RunTests) {
        Invoke-DeveloperCmd -Step "ctest $($TargetInfo.Name)" -Command "ctest --test-dir `"$buildDir`" -C $Config --output-on-failure"
    }

    Write-Host ("=== {0} OK ===" -f $TargetInfo.Name) -ForegroundColor Green
}

function Read-MenuNumber {
    param(
        [string]$Prompt,
        [int]$Min,
        [int]$Max
    )

    while ($true) {
        $inputValue = Read-Host $Prompt
        $number = 0
        if ([int]::TryParse($inputValue, [ref]$number) -and $number -ge $Min -and $number -le $Max) {
            return $number
        }

        Write-Host "Please enter a number between $Min and $Max." -ForegroundColor Yellow
    }
}

function Show-Menu {
    $actions = @("build", "clean", "rebuild", "list")
    $targets = @("all", "modules") + @($ModuleInfos | ForEach-Object { $_.Name })
    $configs = @("Debug", "Release", "RelWithDebInfo", "MinSizeRel")

    while ($true) {
        Write-Host ""
        Write-Host "Select action:" -ForegroundColor Cyan
        for ($i = 0; $i -lt $actions.Count; $i++) {
            Write-Host ("  [{0}] {1}" -f ($i + 1), $actions[$i])
        }
        Write-Host "  [0] exit" -ForegroundColor DarkGray

        $actionIndex = Read-MenuNumber "Enter number" 0 $actions.Count
        if ($actionIndex -eq 0) { return }

        $selectedAction = $actions[$actionIndex - 1]
        if ($selectedAction -eq "list") {
            Show-Targets
            continue
        }

        Write-Host ""
        Write-Host "Select target:" -ForegroundColor Cyan
        for ($i = 0; $i -lt $targets.Count; $i++) {
            Write-Host ("  [{0}] {1}" -f ($i + 1), $targets[$i])
        }
        Write-Host "  [0] back" -ForegroundColor DarkGray

        $targetIndex = Read-MenuNumber "Enter number" 0 $targets.Count
        if ($targetIndex -eq 0) { continue }
        $selectedTarget = $targets[$targetIndex - 1]

        $selectedConfig = $Config
        $selectedRunTests = $false
        $selectedInstallDeps = $false
        if ($selectedAction -ne "clean") {
            Write-Host ""
            Write-Host "Select config:" -ForegroundColor Cyan
            for ($i = 0; $i -lt $configs.Count; $i++) {
                Write-Host ("  [{0}] {1}" -f ($i + 1), $configs[$i])
            }
            Write-Host "  [0] back" -ForegroundColor DarkGray

            $configIndex = Read-MenuNumber "Enter number" 0 $configs.Count
            if ($configIndex -eq 0) { continue }
            $selectedConfig = $configs[$configIndex - 1]

            $testIndex = Read-MenuNumber "Run tests? 0=no, 1=yes" 0 1
            $selectedRunTests = ($testIndex -eq 1)

            $depsIndex = Read-MenuNumber "Run vcpkg install? 0=no, 1=yes" 0 1
            $selectedInstallDeps = ($depsIndex -eq 1)
        }

        $script:Config = $selectedConfig
        $script:RunTests = $selectedRunTests
        $script:InstallDeps = $selectedInstallDeps

        Invoke-Action -SelectedAction $selectedAction -RawTargets @($selectedTarget)
    }
}

function Invoke-Action {
    param(
        [string]$SelectedAction,
        [string[]]$RawTargets
    )

    if ($SelectedAction -eq "list") {
        Show-Targets
        return
    }

    $selectedTargets = @(Resolve-Targets $RawTargets)

    if ($SelectedAction -eq "clean" -or $SelectedAction -eq "rebuild") {
        foreach ($targetInfo in $selectedTargets) {
            Invoke-Clean $targetInfo
        }
    }

    if ($SelectedAction -eq "build" -or $SelectedAction -eq "rebuild") {
        Initialize-BuildEnvironment
        foreach ($targetInfo in $selectedTargets) {
            Invoke-Build $targetInfo
        }
    }
}

try {
    if ($PSBoundParameters.Count -eq 0) {
        Show-Menu
    } else {
        Invoke-Action -SelectedAction $Action -RawTargets $Target
    }
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}
