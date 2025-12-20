#pragma once

// client 侧使用
#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888

// server 侧使用
#define PORT 8888

#define BUF_SIZE 1024
#define MAX_CONN 10
#define HEARTBEAT_INTERVAL 5
#define HEARTBEAT_TIMEOUT 3

// 简单防刷屏：每秒最多允许发送的“用户消息”（广播/私聊）条数
#define RATE_LIMIT_WINDOW_SEC 1
#define RATE_LIMIT_MAX_PER_WINDOW 5

// 与服务端一致的消息分隔符
#define MSG_DELIMITER '\n'
#define DELIMITER_STR "\n"

// client 侧限制
#define CLIENT_MSG_LIMIT 100
#define SEND_BUF_SIZE (BUF_SIZE + 4)
