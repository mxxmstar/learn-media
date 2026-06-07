param(
    [ValidateSet('build', 'rebuild', 'clean', 'clean-cache', 'test', 'help')]
    [string]$Action = 'build',
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Config = 'Debug',
    [switch]$RunTests,
    [switch]$InstallDeps
)

& (Join-Path $PSScriptRoot '..\..\..\script\build-module.ps1') `
    -ModuleName 'media/encoder' `
    -SourceDir $PSScriptRoot `
    -Action $Action `
    -Config $Config `
    -RunTests:$RunTests `
    -InstallDeps:$InstallDeps
exit $LASTEXITCODE
