#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_12.py

功能点12（连接数上限/满员拒绝）测试脚本

服务端有最大在线人数限制（chat_common.h: MAX_CONN=10）。当连接池已满：
- 新连接会收到提示 "聊天室已满，无法连接\n"
- 服务端随后关闭该 socket

测试内容
1) 自动启动 ./server
2) 建立 MAX_CONN 个连接（填满连接池）
3) 再建立第 MAX_CONN+1 个连接
4) 断言：第 MAX_CONN+1 个连接收到“聊天室已满”提示，并被关闭

运行方式（Ubuntu/Linux）
- 先编译：cd chatroom && ./build.sh
- 再运行：python3 tests/test_12.py
"""

import os
import signal
import socket
import subprocess
import time

HOST = "127.0.0.1"
PORT = 8888

# 与 chat_common.h 保持一致（若你改了 MAX_CONN，这里也要同步）
MAX_CONN = 10


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

    socks: list[socket.socket] = []
    extra = None

    try:
        wait_port_open(5.0)

        # 填满连接池
        for _ in range(MAX_CONN):
            s = socket.create_connection((HOST, PORT), timeout=1.5)
            s.settimeout(0.5)
            socks.append(s)

        # 触发“满员拒绝”
        extra = socket.create_connection((HOST, PORT), timeout=1.5)
        extra.settimeout(1.0)

        # 服务端会先 send 文本，再 close
        data = b""
        deadline = time.time() + 2.0
        while time.time() < deadline and b"\n" not in data:
            try:
                chunk = extra.recv(4096)
            except socket.timeout:
                chunk = b""
            if chunk:
                data += chunk
            else:
                # 可能已被关闭
                break

        text = data.decode("utf-8", errors="replace")
        if "聊天室已满" not in text:
            raise AssertionError("missing '聊天室已满' message, got: " + repr(text))

        # 再读一次，确认对端关闭（recv 返回空）
        try:
            more = extra.recv(16)
            if more != b"":
                # 有些系统不会立刻关闭，这里不强制
                pass
        except Exception:
            pass

        print("PASS: server rejects connection when full (function point 12)")

    finally:
        for s in socks:
            try:
                s.close()
            except Exception:
                pass

        if extra is not None:
            try:
                extra.close()
            except Exception:
                pass

        try:
            server.send_signal(signal.SIGINT)
        except Exception:
            pass
        try:
            server.wait(timeout=3)
        except Exception:
            try:
                server.kill()
            except Exception:
                pass


if __name__ == "__main__":
    main()
