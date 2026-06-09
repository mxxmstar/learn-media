<#
.SYNOPSIS
  为 grpc 模块生成 C++ protobuf 和 gRPC 源码。

.DESCRIPTION
  使用 protoc 和 grpc_cpp_plugin 从 .proto 文件生成 C++ 代码。
  自动在 vcpkg_installed 目录中查找工具链。

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File src-cpp\modules\grpc\script\generate-proto.ps1

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File src-cpp\modules\grpc\script\generate-proto.ps1 -OutDir src-cpp\modules\grpc\generated\cpp
#>

[CmdletBinding()]
param(
    [string[]]$ProtoFile,               # 要编译的 .proto 文件列表
    [string]$OutDir,                     # 输出目录
    [string]$Triplet = "x64-windows",    # vcpkg triplet
    [string]$VcpkgInstalledDir,          # vcpkg_installed 目录路径
    [string]$ProtocPath,                 # protoc 可执行文件路径
    [string]$GrpcCppPluginPath           # grpc_cpp_plugin 可执行文件路径
)

$ErrorActionPreference = "Stop"

# 在候选路径中找到第一个存在的文件
function Resolve-ExistingFile {
    param(
        [string[]]$Candidates,
        [string]$ToolName
    )

    foreach ($candidate in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $joined = ($Candidates | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }) -join "`n  "
    throw "未找到 $ToolName。检查路径:`n  $joined"
}

# 计算项目目录结构
$ScriptDir = Split-Path -Parent $PSCommandPath
$ModuleDir = (Resolve-Path -LiteralPath (Join-Path $ScriptDir "..")).Path
$CppDir = (Resolve-Path -LiteralPath (Join-Path $ModuleDir "..\..")).Path
$RootDir = (Resolve-Path -LiteralPath (Join-Path $CppDir "..")).Path

# 默认参数
if ($null -eq $ProtoFile -or $ProtoFile.Count -eq 0) {
    $ProtoFile = @(
        (Join-Path $ModuleDir "proto\hello.proto"),
        (Join-Path $ModuleDir "proto\video_frame.proto")
    )
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $ModuleDir "generated\cpp"
}

# 解析路径
$ProtoPaths = @($ProtoFile | ForEach-Object {
    (Resolve-Path -LiteralPath $_).Path
})
$ProtoDir = Join-Path $ModuleDir "proto"
if ([System.IO.Path]::IsPathRooted($OutDir)) {
    $OutputDir = [System.IO.Path]::GetFullPath($OutDir)
} else {
    $OutputDir = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutDir))
}

# 查找 vcpkg 安装目录
$InstalledRoots = @()
if (-not [string]::IsNullOrWhiteSpace($VcpkgInstalledDir)) {
    $InstalledRoots += [System.IO.Path]::GetFullPath($VcpkgInstalledDir)
} else {
    $InstalledRoots += Join-Path $CppDir "vcpkg_installed"
    $InstalledRoots += Join-Path $RootDir "vcpkg_installed"
}

# 查找 protoc
if ([string]::IsNullOrWhiteSpace($ProtocPath)) {
    $ProtocPath = Resolve-ExistingFile `
        -ToolName "protoc.exe" `
        -Candidates ($InstalledRoots | ForEach-Object {
            Join-Path $_ "$Triplet\tools\protobuf\protoc.exe"
        })
} else {
    $ProtocPath = (Resolve-Path -LiteralPath $ProtocPath).Path
}

# 查找 grpc_cpp_plugin
if ([string]::IsNullOrWhiteSpace($GrpcCppPluginPath)) {
    $GrpcCppPluginPath = Resolve-ExistingFile `
        -ToolName "grpc_cpp_plugin.exe" `
        -Candidates ($InstalledRoots | ForEach-Object {
            Join-Path $_ "$Triplet\tools\grpc\grpc_cpp_plugin.exe"
        })
} else {
    $GrpcCppPluginPath = (Resolve-Path -LiteralPath $GrpcCppPluginPath).Path
}

# 创建输出目录
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

Write-Host "protoc: $ProtocPath"
Write-Host "grpc_cpp_plugin: $GrpcCppPluginPath"
Write-Host "proto 目录: $ProtoDir"
foreach ($path in $ProtoPaths) {
    Write-Host "proto: $path"
}
Write-Host "输出: $OutputDir"

# 运行 protoc
$protocArgs = @(
    "--proto_path=$ProtoDir",
    "--cpp_out=$OutputDir",
    "--grpc_out=$OutputDir",
    "--plugin=protoc-gen-grpc=$GrpcCppPluginPath",
    $ProtoPaths
)

& $ProtocPath @protocArgs
if ($LASTEXITCODE -ne 0) {
    throw "protoc 失败，退出码 $LASTEXITCODE."
}

Write-Host "生成的 C++ protobuf 文件:"
Get-ChildItem -LiteralPath $OutputDir -Filter "*.pb.*" |
    Sort-Object Name |
    ForEach-Object { Write-Host "  $($_.FullName)" }
