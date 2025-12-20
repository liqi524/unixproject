#pragma once

#include "server_types.h"

// 初始化线程池：启动 worker_count 个工作线程
int server_thread_pool_init(ConnPool* pool, volatile int* running, int worker_count);

// 将新连接任务投递给线程池（worker 会负责处理整个连接生命周期）
int server_thread_pool_enqueue_client(int client_fd);

// 停止线程池并等待所有 worker 退出
void server_thread_pool_shutdown(void);
