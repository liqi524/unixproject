#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_14.py - 功能点14：连接池互斥/竞态条件修复（引用计数延迟释放）压力测试

目标（可观察行为）：
- 在“高频连接/断开 + 并发 /who 查询 + 心跳线程删除连接”的情况下，服务端不应崩溃。

为何需要这个测试：
- 连接池节点可能同时被 worker 线程（处理会话）与心跳线程（超时剔除）删除。
- 若删除路径直接 free 节点，而 worker 仍持有指针，会出现 use-after-free，表现为随机崩溃。

测试方法：
- 启动 ./server
- 并发启动若干客户端线程：
  - N 个“短连接风暴”线程：循环 connect -> 发送一条消息 -> 立即 close
  - 1 个“/who 压测”线程：保持连接，持续发送 /who 并读取响应，同时自动 PING->PONG
- 运行几秒后检查 server 进程仍存活；最后优雅停止 server

运行环境：
- 设计在 Ubuntu/Linux 下运行（与项目其他 tests 保持一致）
"""

import os
import signal
import socket
import subprocess
import threading
import time
import select

HOST = "127.0.0.1"
PORT = 8888


def wait_port(host: str, port: int, timeout_s: float = 3.0) -> None:
    deadline = time.time() + timeout_s
    last_err = None
    while time.time() < deadline:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.2)
        try:
            s.connect((host, port))
            s.close()
            return
        except OSError as e:
            last_err = e
            time.sleep(0.05)
        finally:
            try:
                s.close()
            except Exception:
                pass
    raise RuntimeError(f"server 端口未就绪: {host}:{port}, last_err={last_err}")


def recv_lines_and_respond_ping(sock: socket.socket, stop_event: threading.Event) -> None:
    """尽量读干净 socket 的数据；遇到 PING 就回复 PONG。"""
    sock.setblocking(False)
    buf = b""

    while not stop_event.is_set():
        r, _, _ = select.select([sock], [], [], 0.05)
        if not r:
            return
        try:
            data = sock.recv(4096)
        except BlockingIOError:
            return
        if not data:
            return
        buf += data
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            if line == b"PING":
                try:
                    sock.sendall(b"PONG\n")
                except OSError:
                    return


def short_connection_storm(stop_event: threading.Event, idx: int) -> None:
    """高频连接/断开：尽量覆盖“心跳剔除/worker 退出/连接池删除”的并发路径。"""
    i = 0
    while not stop_event.is_set():
        i += 1
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(0.6)
        try:
            s.connect((HOST, PORT))
            # 发一条普通消息（会进入广播队列），然后立刻断开
            msg = f"storm[{idx}]#{i}\n".encode("utf-8")
            s.sendall(msg)
            # 尝试读一小会儿，处理可能出现的 PING
            recv_lines_and_respond_ping(s, stop_event)
        except OSError:
            # 压测下允许偶发失败（例如正在关闭），不影响核心目标
            pass
        finally:
            try:
                s.close()
            except Exception:
                pass

        # 限速：避免短连接风暴把 CPU/调度打满，影响交互式终端输入体验
        time.sleep(0.01)


def who_spammer(stop_event: threading.Event) -> None:
    """保持长连接并持续 /who：验证连接池遍历与并发删除不会崩溃。"""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(0.8)
    try:
        s.connect((HOST, PORT))
        s.setblocking(False)
        while not stop_event.is_set():
            try:
                s.sendall(b"/who\n")
            except OSError:
                return
            # 读取响应（包含多行），并处理心跳
            recv_lines_and_respond_ping(s, stop_event)
            time.sleep(0.05)
    finally:
        try:
            s.close()
        except Exception:
            pass


def main() -> None:
    root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))

    # 启动 server
    proc = subprocess.Popen(
        [os.path.join(root, "server")],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.STDOUT,
        preexec_fn=os.setsid if hasattr(os, "setsid") else None,
    )

    try:
        wait_port(HOST, PORT, timeout_s=3.0)

        stop_event = threading.Event()

        threads = []
        # 1) /who 压测线程
        threads.append(threading.Thread(target=who_spammer, args=(stop_event,), daemon=True))

        # 2) 短连接风暴线程（数量不要过大，避免把测试跑成 DoS）
        for idx in range(3):
            threads.append(
                threading.Thread(target=short_connection_storm, args=(stop_event, idx), daemon=True)
            )

        for t in threads:
            t.start()

        # 压测时长：时间略短一些，避免影响机器可交互性
        time.sleep(3.0)

        # 关键断言：server 仍存活
        code = proc.poll()
        assert code is None, f"server 进程异常退出，exit_code={code}"

        print("PASS: server 在并发连接/删除压力下未崩溃（功能点14）")

    finally:
        try:
            stop_event.set()
        except Exception:
            pass

        # 尝试优雅停止 server
        try:
            if proc.poll() is None:
                if hasattr(os, "killpg") and hasattr(os, "getpgid"):
                    os.killpg(os.getpgid(proc.pid), signal.SIGINT)
                else:
                    proc.send_signal(signal.SIGINT)
                proc.wait(timeout=2.0)
        except Exception:
            try:
                proc.terminate()
            except Exception:
                pass

        # 兜底：确保 server 不残留（残留会持续占用 CPU/端口，导致后续命令行体验变差）
        try:
            if proc.poll() is None:
                proc.kill()
        except Exception:
            pass


if __name__ == "__main__":
    main()
