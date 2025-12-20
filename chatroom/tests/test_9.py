#!/usr/bin/env python3
"""test_9.py

知识点 9（线程池）测试脚本

测试目标
- 验证服务端采用线程池模型：固定数量 worker 线程处理连接，不会为每个客户端新建线程。

核心验证思路（可自动化、无需人工盯日志）
- 在 Linux 上，每个进程的线程数可从 /proc/<pid>/status 的 "Threads:" 读取。
- 我们启动 ./server，记录它的线程数 baseline。
- 然后快速创建多个客户端连接（达到 MAX_CONN=10 上限），并发送/关闭。
- 如果是“每连接一线程”，线程数会随着客户端数显著增长；
  如果是“线程池”，线程数应保持在一个较小常数范围（≈ baseline）。

运行方式（Ubuntu）
1) 先编译 server（确保包含线程池模块 src/server_thread_pool.c）
2) 运行：python3 tests/test_9.py

预期结果
- PASS：线程数峰值 <= worker_count + extra_overhead
- FAIL：线程数明显随连接数增长（说明还在每连接一线程）

备注
- 本脚本会自行启动/停止服务端，不需要你手动 ./server。
- worker_count 当前在 server.c 里写死为 4；如果你改了这个数字，需要同步调整脚本里的 WORKER_COUNT。
"""

import os
import signal
import socket
import subprocess
import time

HOST = "127.0.0.1"
PORT = 8888

# 与 server.c 中 worker_count 保持一致
WORKER_COUNT = 4

# 线程池外的额外线程开销：main + heartbeat + dispatch (+ 可能的 console)
# 再留一点余量，避免不同系统实现导致的轻微波动。
EXTRA_OVERHEAD = 8

# 连接池 MAX_CONN=10；测试时不要超过这个，否则会被服务端拒绝。
CLIENTS = 10


def read_threads(pid: int) -> int:
    """读取 /proc/<pid>/status 中的 Threads: 数值（Linux-only）。"""
    with open(f"/proc/{pid}/status", "r", encoding="utf-8") as f:
        for line in f:
            if line.startswith("Threads:"):
                return int(line.split(":", 1)[1].strip())
    raise RuntimeError("Threads field not found")


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
    if not os.path.exists("./server"):
        raise SystemExit("ERROR: ./server not found. Please compile server first.")

    # 启动服务端；stdin 用 DEVNULL 避免脚本卡在控制台读取
    proc = subprocess.Popen(
        ["./server"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    try:
        wait_port_open(5.0)

        pid = proc.pid
        base_threads = read_threads(pid)

        # 创建最大连接数的客户端，保持短时间连接
        socks = []
        for _ in range(CLIENTS):
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.connect((HOST, PORT))
            s.settimeout(0.2)
            socks.append(s)

        # 让服务端有时间把连接分发给 worker
        time.sleep(0.5)

        peak_threads = base_threads
        # 在连接存在期间多次采样，取峰值
        for _ in range(10):
            t = read_threads(pid)
            peak_threads = max(peak_threads, t)
            time.sleep(0.1)

        # 关闭所有客户端（触发服务端会话结束）
        for s in socks:
            try:
                # 主动 exit 更“温和”，也能加速回收
                s.sendall(b"exit\n")
            except Exception:
                pass
            try:
                s.close()
            except Exception:
                pass

        # 再采样一次，确保没有因为连接数而激增
        time.sleep(0.5)
        after_threads = read_threads(pid)
        peak_threads = max(peak_threads, after_threads)

        # 判定阈值：线程池模式下线程数应基本恒定
        max_allowed = WORKER_COUNT + EXTRA_OVERHEAD

        if peak_threads > max_allowed:
            print("FAIL: thread count too high, seems not using a fixed-size thread pool")
            print(f"  base_threads={base_threads}")
            print(f"  peak_threads={peak_threads}")
            print(f"  max_allowed={max_allowed} (WORKER_COUNT={WORKER_COUNT}, EXTRA_OVERHEAD={EXTRA_OVERHEAD})")
            raise SystemExit(1)

        print("PASS: thread pool works (knowledge point 9)")
        print(f"  base_threads={base_threads}")
        print(f"  peak_threads={peak_threads}")
        print(f"  max_allowed={max_allowed}")

    finally:
        # 停止服务端
        try:
            proc.send_signal(signal.SIGINT)
        except Exception:
            pass
        try:
            proc.wait(timeout=3)
        except Exception:
            try:
                proc.kill()
            except Exception:
                pass


if __name__ == "__main__":
    main()
