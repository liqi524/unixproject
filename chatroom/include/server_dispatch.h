#pragma once

#include "server_types.h"

// 初始化并启动广播调度线程（内部使用 pthread_cond_t）
int server_dispatch_init(ConnPool* pool, volatile int* running);

// 停止广播调度线程并释放资源
void server_dispatch_shutdown(void);

// 将一条广播消息入队（由调度线程负责发送）
int server_dispatch_enqueue_broadcast(int sender_fd, const char* msg);

// 给新连接发送最近的历史消息（每条一行，带 '\n'）
void server_dispatch_send_history(int client_fd);
