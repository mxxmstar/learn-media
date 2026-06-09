"""帧抓取服务端 —— 通过 gRPC 接收并可选地持久化视频帧到磁盘。"""

from pathlib import Path
import argparse
import time

from grpc_server import GrpcServer
from proto_loader import load_video_frame_proto


video_frame_pb2, video_frame_pb2_grpc = load_video_frame_proto()

# 当前协议 schema 版本号
SCHEMA_VERSION = 1


def unix_time_ms() -> int:
    """返回当前 Unix 时间戳（毫秒）。"""
    return int(time.time() * 1000)


def safe_extension(image_format: str) -> str:
    """将图片格式字符串清理为安全的文件扩展名。

    将 'jpeg' 映射为 'jpg'，对不安全的字符回退为 'bin'。
    """
    value = (image_format or "bin").lower().strip().lstrip(".")
    if value == "jpeg":
        return "jpg"
    if not value.replace("_", "").replace("-", "").isalnum():
        return "bin"
    return value or "bin"


class VideoFrameIngestService(video_frame_pb2_grpc.VideoFrameIngestServicer):
    """处理 FrameRequest 消息并将帧持久化到磁盘的 gRPC 服务实现。"""

    def __init__(self, save_dir: Path | None) -> None:
        self.save_dir = save_dir
        if self.save_dir is not None:
            self.save_dir.mkdir(parents=True, exist_ok=True)

    def SendFrame(self, request, context):
        """处理单个一元 FrameRequest，返回 FrameAck。"""
        frame_id = f"{request.video_id}:{request.frame_index}"
        version_ack = self._check_schema_version(request, frame_id)
        if version_ack is not None:
            return version_ack

        self._save_frame(request)
        print(
            "received frame "
            f"id={frame_id} "
            f"ts={request.timestamp_ms}ms "
            f"format={request.image_format} "
            f"size={request.width}x{request.height} "
            f"bytes={len(request.image_data)} "
            f"prompt={request.prompt!r}",
            flush=True,
        )
        return video_frame_pb2.FrameAck(
            ok=True,
            message="frame received",
            frame_id=frame_id,
            received_bytes=len(request.image_data),
            server_time_ms=unix_time_ms(),
        )

    def SendFrameStream(self, request_iterator, context):
        """处理客户端流式 FrameRequest 批量，返回单个 FrameAck。"""
        count = 0
        total_bytes = 0
        last_frame_id = ""
        for request in request_iterator:
            version_ack = self._check_schema_version(
                request,
                f"{request.video_id}:{request.frame_index}",
            )
            if version_ack is not None:
                return version_ack

            count += 1
            total_bytes += len(request.image_data)
            last_frame_id = f"{request.video_id}:{request.frame_index}"
            self._save_frame(request)
            print(
                f"received stream frame id={last_frame_id} bytes={len(request.image_data)}",
                flush=True,
            )

        return video_frame_pb2.FrameAck(
            ok=True,
            message=f"stream received {count} frame(s)",
            frame_id=last_frame_id,
            received_bytes=total_bytes,
            server_time_ms=unix_time_ms(),
        )

    def _save_frame(self, request) -> None:
        """将收到的帧图片数据写入保存目录。"""
        if self.save_dir is None:
            return
        ext = safe_extension(request.image_format)
        video_id = request.video_id or "video"
        filename = f"{video_id}_{request.frame_index:06d}.{ext}"
        path = self.save_dir / filename
        path.write_bytes(request.image_data)

    def _check_schema_version(self, request, frame_id: str):
        """检查 schema 版本是否匹配，不匹配则返回错误 FrameAck。"""
        if request.schema_version == SCHEMA_VERSION:
            return None
        return video_frame_pb2.FrameAck(
            ok=False,
            message=(
                f"schema version mismatch: client={request.schema_version}, "
                f"server={SCHEMA_VERSION}"
            ),
            frame_id=frame_id,
            received_bytes=0,
            server_time_ms=unix_time_ms(),
        )


def parse_args() -> argparse.Namespace:
    """解析帧服务端演示的命令行参数。"""
    parser = argparse.ArgumentParser(description="Minimal Python gRPC frame receiver.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=18080)
    parser.add_argument("--save-dir", type=Path, default=None)
    parser.add_argument("--workers", type=int, default=4)
    return parser.parse_args()


class FrameGrpcServer(GrpcServer):
    """注册 VideoFrameIngest 服务的具体 gRPC 服务器。"""

    def __init__(self, host: str, port: int, workers: int, save_dir: Path | None) -> None:
        super().__init__(host=host, port=port, workers=workers)
        self.save_dir = save_dir

    def register_services(self, server) -> None:
        """将 VideoFrameIngestService 附加到 gRPC 服务构建器。"""
        video_frame_pb2_grpc.add_VideoFrameIngestServicer_to_server(
            VideoFrameIngestService(self.save_dir),
            server,
        )


def main() -> None:
    """入口：启动帧服务端并阻塞直到被中断。"""
    args = parse_args()
    server = FrameGrpcServer(
        host=args.host,
        port=args.port,
        workers=args.workers,
        save_dir=args.save_dir,
    )
    server.start()
    print(f"frame server listening on {server.address}", flush=True)
    if args.save_dir is not None:
        print(f"saving received frames to {args.save_dir}", flush=True)
    try:
        server.wait()
    except KeyboardInterrupt:
        print("stopping frame server", flush=True)
        server.stop(grace=1)


if __name__ == "__main__":
    main()
