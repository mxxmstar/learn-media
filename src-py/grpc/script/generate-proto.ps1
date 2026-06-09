<#
.SYNOPSIS
  为 src-py 生成 Python protobuf 和 gRPC 模块。

.DESCRIPTION
  使用 Python 的 grpc_tools.protoc 模块从 .proto 文件生成 Python 代码。

.EXAMPLE
  powershell -ExecutionPolicy Bypass -File src-py\script\generate-proto.ps1
#>

[CmdletBinding()]
param(
    [string]$ProtoFile,              # .proto 文件路径
    [string]$OutDir,                 # 输出目录
    [string]$PythonExe = "python"    # Python 可执行文件
)

$ErrorActionPreference = "Stop"

# 计算项目目录结构
$ScriptDir = Split-Path -Parent $PSCommandPath
$PyGrpcDir = (Resolve-Path -LiteralPath (Join-Path $ScriptDir "..")).Path
$RootDir = (Resolve-Path -LiteralPath (Join-Path $PyGrpcDir "..\..")).Path

# 默认参数
if ([string]::IsNullOrWhiteSpace($ProtoFile)) {
    $ProtoFile = Join-Path $RootDir "src-cpp\modules\grpc\proto\video_frame.proto"
}
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $PyGrpcDir "generated"
}

# 解析路径
$ProtoPath = (Resolve-Path -LiteralPath $ProtoFile).Path
$ProtoDir = Split-Path -Parent $ProtoPath
if ([System.IO.Path]::IsPathRooted($OutDir)) {
    $OutputDir = [System.IO.Path]::GetFullPath($OutDir)
} else {
    $OutputDir = [System.IO.Path]::GetFullPath((Join-Path (Get-Location).Path $OutDir))
}

# 创建输出目录
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# 检查 grpc_tools 是否可用
& $PythonExe -c "import grpc_tools.protoc" 2>$null
if ($LASTEXITCODE -ne 0) {
    throw "Python 包 grpcio-tools 不可用。运行: $PythonExe -m pip install -r src-py\grpc\requirements.txt"
}

Write-Host "python: $PythonExe"
Write-Host "proto: $ProtoPath"
Write-Host "输出: $OutputDir"

# 运行 grpc_tools.protoc
& $PythonExe -m grpc_tools.protoc `
    "-I$ProtoDir" `
    "--python_out=$OutputDir" `
    "--grpc_python_out=$OutputDir" `
    "$ProtoPath"

if ($LASTEXITCODE -ne 0) {
    throw "grpc_tools.protoc 失败，退出码 $LASTEXITCODE."
}

Write-Host "生成的 Python protobuf 文件:"
Get-ChildItem -LiteralPath $OutputDir -Filter "video_frame_pb2*.py" |
    Sort-Object Name |
    ForEach-Object { Write-Host "  $($_.FullName)" }
