"""gRPC 服务器基类，封装线程池执行器与生命周期管理。"""

from __future__ import annotations

from concurrent import futures

import grpc


class GrpcServer:
    """gRPC 服务器基类。

    处理构建器设置（无限消息大小、自定义线程池）、
    通过抽象方法 *register_services* 注册服务，
    以及 start / wait / stop 生命周期管理。
    """

    def __init__(self, host: str = "127.0.0.1", port: int = 18080, workers: int = 4) -> None:
        """配置服务器参数。

        Args:
            host: 监听地址。
            port: 监听端口。
            workers: 线程池最大工作线程数。
        """
        self.host = host
        self.port = port
        self.workers = workers
        self.server: grpc.Server | None = None

    @property
    def address(self) -> str:
        """返回 'host:port' 格式的地址字符串。"""
        return f"{self.host}:{self.port}"

    def register_services(self, server: grpc.Server) -> None:
        """子类重写此方法以注册具体的 gRPC 服务实现。"""
        raise NotImplementedError

    def start(self) -> None:
        """构建服务器、注册服务、绑定端口并启动。"""
        server = grpc.server(
            futures.ThreadPoolExecutor(max_workers=self.workers),
            options=[
                ("grpc.max_receive_message_length", -1),
                ("grpc.max_send_message_length", -1),
            ],
        )

        self.register_services(server)
        bound_port = server.add_insecure_port(self.address)
        if bound_port == 0:
            raise RuntimeError(f"gRPC 服务器绑定到 {self.address} 失败")
        server.start()
        self.server = server

    def wait(self) -> None:
        """阻塞当前线程直到服务器终止。"""
        if self.server is not None:
            self.server.wait_for_termination()

    def stop(self, grace: float = 1.0) -> None:
        """发起优雅关闭，最多等待 *grace* 秒给正在处理的 RPC 完成。"""
        if self.server is not None:
            self.server.stop(grace=grace)
            self.server = None
