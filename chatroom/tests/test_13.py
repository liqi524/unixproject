#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_13.py

功能点13（服务端优雅退出/资源清理）测试脚本

服务端实现了优雅退出流程（见 server.c）：
- 捕获 SIGINT/SIGTERM（例如 Ctrl+C）
- 停止 accept
- 关闭所有客户端 socket，使 worker 线程里的 recv 解除阻塞并退出
- 停止 dispatch 线程、停止线程池

测试内容
1) 自动启动 ./server
2) 建立 2 个客户端连接
3) 给服务端发送 SIGINT
4) 断言：服务端进程在短时间内退出
5) 断言：客户端连接被关闭（recv 返回空 或 send/recv 报错）

运行方式（Ubuntu/Linux）
- 先编译：cd chatroom && ./build.sh
- 再运行：python3 tests/test_13.py
"""

import os
import signal
import socket
import subprocess
import time

HOST = "127.0.0.1"
PORT = 8888


def wait_port_open(timeout_s: float = 5.0) -> None:
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        try:
            s = socket.create_connection((HOST, PORT), timeout=0.2)
            s.close()
            return
        except OSError:
            time.sleep(0.05)
    raise RuntimeError("server port not open")


def socket_is_closed(s: socket.socket) -> bool:
    s.settimeout(0.5)
    try:
        data = s.recv(1)
        return data == b""
    except socket.timeout:
        # 没读到也可能没关闭；再试发一条探测
        try:
            s.sendall(b"/status\n")
        except OSError:
            return True
        return False
    except OSError:
        return True


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    if not os.path.exists(os.path.join(root, "server")):
        raise SystemExit("ERROR: ./server not found. Run ./build.sh first.")

    server = subprocess.Popen(
        [os.path.join(root, "server")],
        cwd=root,
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    a = b = None
    try:
        wait_port_open(5.0)
        a = socket.create_connection((HOST, PORT), timeout=2)
        b = socket.create_connection((HOST, PORT), timeout=2)

        # 让服务端/worker 就绪
        time.sleep(0.2)

        # 触发优雅退出
        server.send_signal(signal.SIGINT)

        # 服务端应快速退出
        try:
            server.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            raise AssertionError("server did not exit in time after SIGINT")

        # 客户端连接应被关闭
        if a is not None and not socket_is_closed(a):
            raise AssertionError("client A socket not closed after server shutdown")
        if b is not None and not socket_is_closed(b):
            raise AssertionError("client B socket not closed after server shutdown")

        print("PASS: server graceful shutdown closes client sockets (function point 13)")

    finally:
        for s in (a, b):
            if s is None:
                continue
            try:
                s.close()
            except Exception:
                pass

        # 兜底清理 server
        try:
            if server.poll() is None:
                server.kill()
        except Exception:
            pass


if __name__ == "__main__":
    main()
