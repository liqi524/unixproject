#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_2.py

功能点2（基础网络通信/协议收发）测试脚本

在本项目里，“功能点2”我们用一个最小、可自动化验收的外部行为来体现：
- 客户端能成功 TCP 连接服务端
- 发送一条以 "\n" 结尾的指令（协议分隔符）
- 服务端能返回对应文本（说明服务端的 recv/解析/ send 通路是通的）

测试内容
1) 自动启动 ./server
2) 建立一个 TCP 连接
3) 发送 "/help\n"
4) 断言响应包含指令说明关键字（例如 "/status"、"exit"）

运行方式（Ubuntu/Linux）
- 先编译：cd chatroom && ./build.sh
- 再运行：python3 tests/test_2.py
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


def recv_until_contains(sock: socket.socket, needles: list[str], timeout_s: float = 3.0) -> str:
    sock.settimeout(0.3)
    buf = bytearray()
    collected = ""
    end = time.time() + timeout_s

    while time.time() < end:
        try:
            data = sock.recv(4096)
        except socket.timeout:
            data = b""

        if data:
            buf.extend(data)

        # 按 \n 拆行；遇到 PING 自动回 PONG；其他行累计到 collected
        while b"\n" in buf:
            line, rest = buf.split(b"\n", 1)
            buf[:] = rest
            text = line.decode("utf-8", errors="replace")
            if text == "PING":
                sock.sendall(b"PONG\n")
                continue
            if text:
                collected += text + "\n"

        if all(n in collected for n in needles):
            return collected

    raise TimeoutError("timeout waiting for response containing: " + ",".join(needles))


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

    try:
        wait_port_open(5.0)

        s = socket.create_connection((HOST, PORT), timeout=2)
        try:
            s.sendall(b"/help\n")
            _ = recv_until_contains(s, needles=["/status", "exit"], timeout_s=3.0)
        finally:
            try:
                s.close()
            except Exception:
                pass

        print("PASS: basic connect + send/recv (/help) works (function point 2)")

    finally:
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
