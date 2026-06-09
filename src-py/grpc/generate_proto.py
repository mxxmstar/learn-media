"""使用 grpc_tools.protoc 从 .proto 文件生成 Python protobuf 和 gRPC 桩代码。"""

from pathlib import Path
import sys

from grpc_tools import protoc


# 项目根目录
ROOT_DIR = Path(__file__).resolve().parents[2]
# .proto 文件所在目录
PROTO_DIR = ROOT_DIR / "src-cpp" / "modules" / "grpc" / "proto"
# 需要编译的 .proto 文件
PROTO_FILE = PROTO_DIR / "video_frame.proto"
# 生成的 Python 文件输出目录
GENERATED_DIR = Path(__file__).resolve().parent / "generated"


def generate() -> None:
    """编译 video_frame.proto，在 generated/ 下生成 Python pb2 和 pb2_grpc 模块。"""
    GENERATED_DIR.mkdir(parents=True, exist_ok=True)
    result = protoc.main(
        [
            "grpc_tools.protoc",
            f"-I{PROTO_DIR}",
            f"--python_out={GENERATED_DIR}",
            f"--grpc_python_out={GENERATED_DIR}",
            str(PROTO_FILE),
        ]
    )
    if result != 0:
        raise RuntimeError(f"protoc 编译失败，退出码 {result}")


if __name__ == "__main__":
    try:
        generate()
    except Exception as exc:
        print(f"生成 proto 失败: {exc}", file=sys.stderr)
        raise
