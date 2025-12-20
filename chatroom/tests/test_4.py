#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_4.py

功能点4（多客户端聊天/广播）测试脚本

在本项目里，“功能点4”最典型的验收点是：
- 多个客户端同时在线
- 其中一个客户端发送普通消息（以 \n 分隔）
- 其他客户端都能收到广播
- 发送者本身不会收到自己那条广播（服务端广播逻辑是“除发送者外的所有连接”）

测试内容
1) 自动启动 ./server
2) 建立 3 个 TCP 连接（A/B/C）
3) A 发送 "hello\n"
4) 断言：B/C 都能收到包含 "：hello" 的行（带时间戳与昵称）
5) 断言：A 不应收到包含 "：hello" 的行

运行方式（Ubuntu/Linux）
- 先编译：cd chatroom && ./build.sh
- 再运行：python3 tests/test_4.py
"""

import os
import signal
import socket
import subprocess
import time
import select

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


def pump_sockets(socks: list[socket.socket], bufs: dict[socket.socket, bytearray], outs: dict[socket.socket, list[str]], timeout_s: float) -> None:
    """在 timeout_s 时间内尽可能读取 socket 数据，按 \n 拆行；遇到 PING 自动回 PONG。"""
    end = time.time() + timeout_s
    for s in socks:
        s.setblocking(False)

    while time.time() < end:
        r, _, _ = select.select(socks, [], [], 0.2)
        if not r:
            continue
        for s in r:
            try:
                data = s.recv(4096)
            except BlockingIOError:
                continue
            except OSError:
                continue
            if not data:
                continue
            bufs[s].extend(data)
            buf = bufs[s]
            while b"\n" in buf:
                line, rest = buf.split(b"\n", 1)
                buf[:] = rest
                text = line.decode("utf-8", errors="replace")
                if text == "PING":
                    try:
                        s.sendall(b"PONG\n")
                    except OSError:
                        pass
                    continue
                if text:
                    outs[s].append(text)


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

    a = b = c = None
    try:
        wait_port_open(5.0)
        a = socket.create_connection((HOST, PORT), timeout=2)
        b = socket.create_connection((HOST, PORT), timeout=2)
        c = socket.create_connection((HOST, PORT), timeout=2)

        socks = [a, b, c]
        bufs = {a: bytearray(), b: bytearray(), c: bytearray()}
        outs = {a: [], b: [], c: []}

        # 让服务端把连接加入连接池/worker 就绪
        time.sleep(0.2)

        a.sendall(b"hello\n")

        # 读一会儿，收集广播
        pump_sockets(socks, bufs, outs, timeout_s=3.0)

        got_b = any("：hello" in x and "(id=" in x for x in outs[b])
        got_c = any("：hello" in x and "(id=" in x for x in outs[c])
        got_a = any("：hello" in x and "(id=" in x for x in outs[a])

        if not (got_b and got_c):
            raise AssertionError(
                "B/C did not both receive broadcast.\n"
                + f"B lines={outs[b]}\n"
                + f"C lines={outs[c]}\n"
                + f"A lines={outs[a]}\n"
            )

        if got_a:
            raise AssertionError("Sender A should NOT receive its own broadcast, but did: " + str(outs[a]))

        print("PASS: broadcast to other clients works (function point 4)")

    finally:
        for s in (a, b, c):
            if s is None:
                continue
            try:
                s.close()
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
