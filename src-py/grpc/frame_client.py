"""帧抓取客户端 —— 以可配置的速率向 gRPC 服务端发送视频帧。"""

from __future__ import annotations

from pathlib import Path
import argparse
import time

from grpc_client import GrpcClient
from proto_loader import load_video_frame_proto


video_frame_pb2, video_frame_pb2_grpc = load_video_frame_proto()

# 当前协议 schema 版本号
SCHEMA_VERSION = 1


def unix_time_ms() -> int:
    """返回当前 Unix 时间戳（毫秒）。"""
    return int(time.time() * 1000)


def built_in_tiny_png() -> bytes:
    """返回一个最小的有效 1x1 PNG 图片（67 字节），作为默认发送载荷。"""
    return bytes(
        [
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,
            0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,
            0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,
            0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
            0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,
            0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,
            0x42, 0x60, 0x82,
        ]
    )


def infer_image_format(path: Path | None) -> str:
    """根据文件后缀推断图片格式字符串。

    当 path 为 None 时返回 'png'；将 'jpg' 映射为 'jpeg'。
    """
    if path is None:
        return "png"
    suffix = path.suffix.lower().lstrip(".")
    if suffix == "jpg":
        return "jpeg"
    return suffix or "bin"


class FrameIngestClient(GrpcClient):
    """向 VideoFrameIngest 服务发送 FrameRequest 的 gRPC 客户端。"""

    def __init__(self, target: str) -> None:
        super().__init__(target)
        self.stub = video_frame_pb2_grpc.VideoFrameIngestStub(self.channel)

    def send_frame(
        self,
        *,
        video_id: str,
        frame_index: int,
        timestamp_ms: int,
        image_format: str,
        image_data: bytes,
        width: int,
        height: int,
        prompt: str,
        timeout_seconds: float,
    ):
        """发送单帧到服务端，返回 FrameAck 响应。"""
        request = video_frame_pb2.FrameRequest(
            video_id=video_id,
            frame_index=frame_index,
            timestamp_ms=timestamp_ms,
            image_format=image_format,
            image_data=image_data,
            width=width,
            height=height,
            prompt=prompt,
            schema_version=SCHEMA_VERSION,
            metadata={
                "source": "python_timer_demo",
                "sent_at_unix_ms": str(unix_time_ms()),
            },
        )
        return self.stub.SendFrame(request, timeout=timeout_seconds)


def parse_args() -> argparse.Namespace:
    """解析帧发送演示的命令行参数。"""
    parser = argparse.ArgumentParser(description="Minimal Python gRPC frame sender.")
    parser.add_argument("--server", default="127.0.0.1:18080")
    parser.add_argument("--image", type=Path, default=None)
    parser.add_argument("--format", default=None)
    parser.add_argument("--video-id", default="py-demo-video")
    parser.add_argument("--prompt", default="Describe this frame.")
    parser.add_argument("--fps", type=float, default=1.0)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--timeout-seconds", type=float, default=3.0)
    parser.add_argument("--width", type=int, default=0)
    parser.add_argument("--height", type=int, default=0)
    return parser.parse_args()


def main() -> None:
    """入口：以固定速率发送帧，直到达到指定数量或被中断。"""
    args = parse_args()
    image_data = args.image.read_bytes() if args.image is not None else built_in_tiny_png()
    image_format = args.format or infer_image_format(args.image)
    width = args.width or (1 if args.image is None else 0)
    height = args.height or (1 if args.image is None else 0)
    interval = 1.0 / args.fps

    with FrameIngestClient(args.server) as client:
        print(
            f"sending frames to {args.server}, format={image_format}, "
            f"bytes={len(image_data)}, fps={args.fps}, count={args.count}",
            flush=True,
        )
        frame_index = 0
        while args.count == 0 or frame_index < args.count:
            ack = client.send_frame(
                video_id=args.video_id,
                frame_index=frame_index,
                timestamp_ms=int((1000.0 * frame_index) / args.fps),
                image_format=image_format,
                image_data=image_data,
                width=width,
                height=height,
                prompt=args.prompt,
                timeout_seconds=args.timeout_seconds,
            )
            print(
                f"ack frame={ack.frame_id} ok={ack.ok} "
                f"bytes={ack.received_bytes} message={ack.message}",
                flush=True,
            )
            frame_index += 1
            time.sleep(interval)


if __name__ == "__main__":
    main()
