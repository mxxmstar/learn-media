"""惰性 proto 加载器 —— 自动生成并导入 video_frame protobuf 模块。"""

from pathlib import Path
import importlib
import sys

from generate_proto import GENERATED_DIR, PROTO_FILE, generate


def ensure_generated_proto() -> None:
    """如果生成的输出文件缺失或过期，则重新生成 Python proto 模块。

    比较生成文件与 .proto 源文件的修改时间，必要时重新运行 protoc。
    同时确保生成目录在 sys.path 中以便模块可以被导入。
    """
    pb2 = GENERATED_DIR / "video_frame_pb2.py"
    pb2_grpc = GENERATED_DIR / "video_frame_pb2_grpc.py"
    if (
        not pb2.exists()
        or not pb2_grpc.exists()
        or pb2.stat().st_mtime < PROTO_FILE.stat().st_mtime
        or pb2_grpc.stat().st_mtime < PROTO_FILE.stat().st_mtime
    ):
        generate()

    generated_path = str(Path(GENERATED_DIR))
    if generated_path not in sys.path:
        sys.path.insert(0, generated_path)


def load_video_frame_proto():
    """确保 proto 模块是最新的，返回 (pb2, pb2_grpc) 模块元组。

    Returns:
        (video_frame_pb2, video_frame_pb2_grpc) 模块元组。
    """
    ensure_generated_proto()
    return (
        importlib.import_module("video_frame_pb2"),
        importlib.import_module("video_frame_pb2_grpc"),
    )
