"""gRPC 客户端基类，封装通道生命周期管理与连接检测工具。"""

from __future__ import annotations

from typing import Iterable, Tuple

import grpc


class GrpcClient:
    """gRPC 客户端基类。

    管理一条非安全通道，提供无限消息大小、连接就绪检测以及
    上下文管理器支持等实用功能。
    """

    def __init__(self, target: str, options: Iterable[Tuple[str, int]] | None = None) -> None:
        """打开到 *target*（host:port）的非安全 gRPC 通道。

        Args:
            target: 服务器地址，格式为 'host:port'。
            options: 附加的 gRPC 通道选项。
        """
        channel_options = list(options or [])
        channel_options.append(("grpc.max_receive_message_length", -1))
        channel_options.append(("grpc.max_send_message_length", -1))
        self.target = target
        self.channel = grpc.insecure_channel(target, options=channel_options)

    def wait_for_ready(self, timeout_seconds: float = 5.0) -> bool:
        """阻塞直到通道就绪或超时。

        通道就绪返回 True，超时返回 False。
        """
        try:
            grpc.channel_ready_future(self.channel).result(timeout=timeout_seconds)
            return True
        except grpc.FutureTimeoutError:
            return False

    def close(self) -> None:
        """关闭 gRPC 通道。"""
        self.channel.close()

    def __enter__(self):
        """进入运行时上下文 —— 返回 self。"""
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        """退出运行时上下文 —— 关闭通道。"""
        self.close()
