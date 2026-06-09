<#
.SYNOPSIS
  Generate both C++ and Python protobuf outputs from the shared grpc proto file.

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File script\generate-grpc-proto.ps1
#>

[CmdletBinding()]
param(
    [string]$ProtoFile,
    [string]$CppOutDir,
    [string]$PyOutDir,
    [string]$Triplet = "x64-windows",
    [string]$PythonExe = "python"
)

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $PSCommandPath
$RootDir = (Resolve-Path -LiteralPath (Join-Path $ScriptDir "..")).Path

$UseDefaultProtoFile = [string]::IsNullOrWhiteSpace($ProtoFile)
if ([string]::IsNullOrWhiteSpace($ProtoFile)) {
    $ProtoFile = Join-Path $RootDir "src-cpp\modules\grpc\proto\video_frame.proto"
}
if ([string]::IsNullOrWhiteSpace($CppOutDir)) {
    $CppOutDir = Join-Path $RootDir "src-cpp\modules\grpc\generated\cpp"
}
if ([string]::IsNullOrWhiteSpace($PyOutDir)) {
    $PyOutDir = Join-Path $RootDir "src-py\grpc\generated"
}

$CppScript = Join-Path $RootDir "src-cpp\modules\grpc\script\generate-proto.ps1"
$PyScript = Join-Path $RootDir "src-py\grpc\script\generate-proto.ps1"

if ($UseDefaultProtoFile) {
    & $CppScript -OutDir $CppOutDir -Triplet $Triplet
} else {
    & $CppScript -ProtoFile $ProtoFile -OutDir $CppOutDir -Triplet $Triplet
}
& $PyScript -ProtoFile $ProtoFile -OutDir $PyOutDir -PythonExe $PythonExe

Write-Host "Generated C++ and Python protobuf outputs from one proto source."
