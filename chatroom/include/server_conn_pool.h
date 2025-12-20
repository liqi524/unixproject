#pragma once

#include "server_types.h"

void msg_buf_init(MsgBuffer* buf);
void msg_buf_clear(MsgBuffer* buf);

ConnPool* conn_pool_init(int max_conn);
int conn_pool_add(ConnPool* pool, int fd);
int conn_pool_del(ConnPool* pool, int fd, int print_log);

ConnNode* conn_pool_get_node(ConnPool* pool, int fd);
void conn_pool_put_node(ConnPool* pool, ConnNode* node);

void conn_pool_update_heartbeat(ConnPool* pool, int fd, int reset);
void mark_conn_closed(ConnPool* pool, int fd);
int is_conn_closed(ConnPool* pool, int fd);

// 关闭所有客户端连接（服务端退出时使用）
void conn_pool_close_all(ConnPool* pool, int print_log);
