#pragma once

#include "server_types.h"

// 处理一个客户端连接的完整生命周期（可供线程池 worker 调用）
int client_handle_session(int client_fd, ConnPool* pool);
