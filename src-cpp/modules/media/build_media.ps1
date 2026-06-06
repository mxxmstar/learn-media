param(
    [ValidateSet('build', 'rebuild', 'clean')]
    [string]$Action = 'build'
)

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

$vsDevCmd = Join-Path $vsDir "Common7\Tools\VsDevCmd.bat"
$vcpkgTc = Join-Path $vcpkgDir "scripts\buildsystems\vcpkg.cmake"
$buildDir = Join-Path $PSScriptRoot "build"
$binDir = Join-Path $PSScriptRoot "bin"
$libDir = Join-Path $PSScriptRoot "lib"

if ($Action -eq 'clean') {
    cmd /c """$vsDevCmd"" -arch=x64 -host_arch=x64 && cmake --build ""$buildDir"" --target clean"
    return
}

$cleanFirst = ''
if ($Action -eq 'rebuild') {
    $cleanFirst = '--clean-first'
}

cmd /c """$vsDevCmd"" -arch=x64 -host_arch=x64 && cmake -S . -B ""$buildDir"" -DCMAKE_TOOLCHAIN_FILE=""$vcpkgTc"" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=""$binDir"" -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=""$libDir"" -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=""$libDir"" && cmake --build ""$buildDir"" $cleanFirst"
