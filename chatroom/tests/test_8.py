#!/usr/bin/env python3
"""test_8.py

知识点 8（条件变量 pthread_cond_t）测试脚本

测试目标
- 验证服务端已启用“广播调度线程 + 条件变量队列”的模型：
  client_handler 仅入队并 signal，调度线程 wait/signal 后再广播。

我们从“外部可观察行为”来验证：
1) 启动服务端后，连接两个客户端 A/B。
2) A 发送一条消息 "15"（带 \n 分隔符）。
3) B 应收到广播内容，且格式包含："(id=<n>)：15"（前面会有时间戳与昵称）。

同时覆盖：
- 心跳兼容：服务端发来 "PING\n" 时，脚本会自动回 "PONG\n"，避免因心跳机制影响测试。

运行方式（Ubuntu）
1) 终端1：启动服务端
   ./server

2) 终端2：运行测试
   python3 tests/test_8.py

预期结果
- 通过：打印 "PASS: ..." 并返回码 0
- 失败：打印 B 收到的所有行，并返回码 1
"""

import socket
import threading
import time
from typing import List, Optional

HOST = "127.0.0.1"
PORT = 8888


def _client_worker(name: str, send_once: Optional[str], out: List[str], stop: threading.Event) -> None:
    """一个最小 TCP 客户端：

    - 连接服务端
    - 可选地发送一次消息（send_once）
    - 循环接收并按 \n 拆包成一行行文本
    - 若收到心跳 PING，则自动回复 PONG
    - 将收到的普通文本行记录到 out

    注意：这里不使用你项目中的 client 二进制，而是直接 socket 测试协议层，
    能更稳定地复现/断言服务端广播行为。
    """

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((HOST, PORT))
    s.settimeout(0.5)

    buf = b""
    sent = False

    while not stop.is_set():
        # 只发送一次指定消息
        if send_once is not None and not sent:
            s.sendall((send_once + "\n").encode("utf-8"))
            sent = True

        try:
            data = s.recv(4096)
            if not data:
                break
            buf += data
        except socket.timeout:
            # 没数据就继续循环
            pass

        # 按 \n 拆行，模拟你项目里以 '\n' 作为分隔符的协议
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            text = line.decode("utf-8", errors="replace")

            # 心跳：收到 PING 必须回 PONG，否则可能被服务端踢掉
            if text == "PING":
                s.sendall(b"PONG\n")
                continue

            if text:
                out.append(f"{name}<<<{text}")

    try:
        s.close()
    except Exception:
        pass


def main() -> None:
    # A 会发送 "15"，B 只接收
    out_a: List[str] = []
    out_b: List[str] = []
    stop = threading.Event()

    # 先启动 B，确保它能接到 A 的广播
    t_b = threading.Thread(target=_client_worker, args=("B", None, out_b, stop), daemon=True)
    t_b.start()

    time.sleep(0.2)

    t_a = threading.Thread(target=_client_worker, args=("A", "15", out_a, stop), daemon=True)
    t_a.start()

    # 等待最多 5 秒，直到 B 收到形如：...(id=1)：15
    deadline = time.time() + 5
    ok = False
    while time.time() < deadline:
        for line in list(out_b):
            if "(id=" in line and "：15" in line:
                ok = True
                break
        if ok:
            break
        time.sleep(0.05)

    stop.set()
    time.sleep(0.2)

    if not ok:
        print("FAIL: B did not receive expected broadcast")
        print("B output (each line is one server message line):")
        for l in out_b:
            print(" ", l)
        raise SystemExit(1)

    print("PASS: condvar dispatch broadcast works (knowledge point 8)")


if __name__ == "__main__":
    main()
