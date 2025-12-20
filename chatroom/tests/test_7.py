#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_7.py

功能点7（心跳机制 PING/PONG）测试脚本

本项目心跳机制（服务端侧）：
- 每 HEARTBEAT_INTERVAL 秒向每个在线连接发送 "PING\n"
- 客户端收到 PING 后应回 "PONG\n"
- 若连续 HEARTBEAT_TIMEOUT 次未收到 PONG（或发送失败），服务端会关闭并移除该连接

本测试覆盖两类可观察行为：
A) “会回 PONG 的连接”应保持在线：
   - 能收到 PING
   - 回 PONG 后仍可继续发送指令（例如 /status）并收到响应
B) “不回 PONG 的连接”应在超时后被服务端断开

注意
- 由于心跳间隔与超时次数是固定常量（chat_common.h），本脚本最长需要 ~20 秒。

运行方式（Ubuntu/Linux）
- 先编译：cd chatroom && ./build.sh
- 再运行：python3 tests/test_7.py
"""

import os
import signal
import socket
import subprocess
import time
import select

HOST = "127.0.0.1"
PORT = 8888

# 与 chat_common.h 保持一致（若你改了常量，这里也要同步）
HEARTBEAT_INTERVAL = 5
HEARTBEAT_TIMEOUT = 3


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


def recv_lines(sock: socket.socket, buf: bytearray, auto_pong: bool) -> list[str]:
    """尽量读取一次 socket 缓冲，按 \n 拆行；auto_pong=True 时收到 PING 自动回 PONG。"""
    lines: list[str] = []
    sock.setblocking(False)

    r, _, _ = select.select([sock], [], [], 0.2)
    if not r:
        return lines

    try:
        data = sock.recv(4096)
    except BlockingIOError:
        return lines

    if not data:
        # 对端关闭
        raise ConnectionError("peer closed")

    buf.extend(data)
    while b"\n" in buf:
        line, rest = buf.split(b"\n", 1)
        buf[:] = rest
        text = line.decode("utf-8", errors="replace")
        if text == "PING":
            if auto_pong:
                try:
                    sock.sendall(b"PONG\n")
                except OSError:
                    pass
            lines.append("PING")
            continue
        if text:
            lines.append(text)
    return lines


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

    good = bad = None
    try:
        wait_port_open(5.0)

        good = socket.create_connection((HOST, PORT), timeout=2)
        bad = socket.create_connection((HOST, PORT), timeout=2)

        buf_good = bytearray()
        buf_bad = bytearray()

        # 我们希望至少观察到 1 次 PING
        saw_ping_good = False
        saw_ping_bad = False

        # bad 不回 PONG，等待它被踢出；理论上 ~ HEARTBEAT_INTERVAL*HEARTBEAT_TIMEOUT 秒
        max_wait = HEARTBEAT_INTERVAL * HEARTBEAT_TIMEOUT + 6
        end = time.time() + max_wait

        bad_closed = False
        status_ok = False

        while time.time() < end:
            # good 自动回 PONG
            try:
                lines_g = recv_lines(good, buf_good, auto_pong=True)
                if any(x == "PING" for x in lines_g):
                    saw_ping_good = True
            except ConnectionError:
                raise AssertionError("good connection unexpectedly closed")

            # bad 不回 PONG
            try:
                lines_b = recv_lines(bad, buf_bad, auto_pong=False)
                if any(x == "PING" for x in lines_b):
                    saw_ping_bad = True
            except ConnectionError:
                bad_closed = True

            # 在 good 已经经历过心跳往返后，尝试发 /status 验证仍在线
            if saw_ping_good and not status_ok:
                try:
                    good.sendall(b"/status\n")
                except OSError:
                    raise AssertionError("good connection cannot send /status")
                # 读几轮看是否出现“当前在线人数”
                for _ in range(10):
                    try:
                        lines = recv_lines(good, buf_good, auto_pong=True)
                    except ConnectionError:
                        raise AssertionError("good closed while waiting /status")
                    if any("当前在线人数" in x for x in lines):
                        status_ok = True
                        break

            if bad_closed and status_ok and saw_ping_good:
                break

            time.sleep(0.1)

        if not saw_ping_good:
            raise AssertionError("did not observe PING on good connection")
        if not saw_ping_bad:
            raise AssertionError("did not observe PING on bad connection")
        if not status_ok:
            raise AssertionError("good connection did not get /status response")
        if not bad_closed:
            raise AssertionError("bad connection was not closed by heartbeat timeout")

        print("PASS: heartbeat PING/PONG keeps good alive and kicks bad (function point 7)")

    finally:
        for s in (good, bad):
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
