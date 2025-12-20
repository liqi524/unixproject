#!/usr/bin/env python3
"""test_5.py

知识点 5（异步网络代码实现）测试脚本

本项目中，我们把“异步网络”落地为：客户端使用 select() 同时监听：
- socket（服务端消息、心跳 PING 等）
- stdin（用户输入）
从而在单线程里实现 I/O 多路复用，不再依赖“一个 recv 线程 + 一个主线程”。

测试目标（可自动化验证的部分）
1) 启动服务端 ./server
2) 启动两个客户端 ./client（注意：这里跑的是你项目编译出来的二进制）
3) 向客户端 A 的 stdin 写入一条消息 "15\n"
4) 断言客户端 B 的 stdout 能看到："(id=数字)：15"（带时间戳与昵称）
5) 同时断言 B 的输出里提示符包含 "请输入消息"（且不会黏连乱码）

为什么这个测试能覆盖“异步网络”？
- 客户端是单进程单线程：必须依赖 select() 才能在同一循环里同时处理 stdin 与 socket。
- 如果客户端仍是阻塞式 recv 或只读 stdin，测试会卡住或无法及时输出。

运行方式（Ubuntu）
1) 先编译：
   ./build.sh

2) 运行测试：
   python3 tests/test_5.py

备注
- 本脚本使用 Linux 标准库 pty 创建伪终端，来“自动喂输入 + 读取输出”，不需要第三方库。
"""

import os
import pty
import re
import signal
import subprocess
import time
from select import select


def _spawn_pty_process(argv: list[str]):
    """用伪终端启动一个交互式程序，并返回 (proc, master_fd)。"""
    master_fd, slave_fd = pty.openpty()

    proc = subprocess.Popen(
        argv,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True,
    )

    os.close(slave_fd)
    return proc, master_fd


def _read_until(master_fd: int, pattern: str, timeout: float = 5.0) -> str:
    """从 pty master_fd 读取，直到匹配到正则 pattern 或超时。"""
    buf = ""
    end = time.time() + timeout
    rx = re.compile(pattern)

    while time.time() < end:
        r, _, _ = select([master_fd], [], [], 0.2)
        if not r:
            continue
        try:
            data = os.read(master_fd, 4096)
        except OSError:
            break
        if not data:
            break
        buf += data.decode("utf-8", errors="replace")
        if rx.search(buf):
            return buf

    raise TimeoutError(f"timeout waiting for pattern: {pattern}\n---buffer---\n{buf}\n------------")


def main() -> None:
    if not os.path.exists("./server") or not os.path.exists("./client"):
        raise SystemExit("ERROR: ./server or ./client not found. Run ./build.sh first.")

    server = subprocess.Popen(["./server"], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    a_proc = None
    b_proc = None
    a_fd = None
    b_fd = None

    try:
        # 等待服务端启动
        time.sleep(0.5)

        # 启动两个客户端
        a_proc, a_fd = _spawn_pty_process(["./client"])
        b_proc, b_fd = _spawn_pty_process(["./client"])

        # 等待客户端进入“请输入消息：”状态（提示符出现）
        _read_until(a_fd, r"请输入消息", 5.0)
        _read_until(b_fd, r"请输入消息", 5.0)

        # 给 A 输入一条消息
        os.write(a_fd, b"15\n")

        # B 应收到广播：包含“(id=数字)：15”
        out_b = _read_until(b_fd, r"\(id=\d+\)：15", 5.0)

        # 同时确保提示符仍然存在（说明输出换行/提示符逻辑正常）
        if "请输入消息" not in out_b:
            raise AssertionError("B output missing prompt containing '请输入消息'\n" + out_b)

        print("PASS: async client(select) receives broadcast and keeps prompt (knowledge point 5)")

    finally:
        for p in (a_proc, b_proc):
            if p is None:
                continue
            try:
                p.send_signal(signal.SIGINT)
            except Exception:
                pass
            try:
                p.wait(timeout=1)
            except Exception:
                try:
                    p.kill()
                except Exception:
                    pass

        for fd in (a_fd, b_fd):
            if fd is None:
                continue
            try:
                os.close(fd)
            except Exception:
                pass

        try:
            server.send_signal(signal.SIGINT)
        except Exception:
            pass
        try:
            server.wait(timeout=2)
        except Exception:
            try:
                server.kill()
            except Exception:
                pass


if __name__ == "__main__":
    main()
