#pragma once

#include <pthread.h>

typedef struct ClientState {
    int fd;
} ClientState;

// 知识点5：异步网络（I/O 多路复用）客户端主循环
int client_run_async(ClientState* st);
