#pragma once

#include "chat_common.h"
#include <pthread.h>

#define MAX_NICKNAME_LEN 32

typedef struct MsgBuffer {
    char data[BUF_SIZE * 2];
    int len;
} MsgBuffer;

typedef struct ConnNode {
    int fd;
    int client_id; // 可展示编号：从 1 递增（不等于 OS fd）
    char nickname[MAX_NICKNAME_LEN];
    long rate_window_sec;
    int rate_count;
    int heartbeat_count;
    int is_closed;
    int refcount; // 引用计数：避免并发删除导致悬空指针
    int removed;  // 是否已从连接池链表移除（仅用于延迟释放）
    MsgBuffer msg_buf;
    struct ConnNode* next;
} ConnNode;

typedef struct ConnPool {
    ConnNode* head;
    int max_conn;
    int cur_conn;
    int next_client_id;
    pthread_mutex_t mutex;
} ConnPool;
