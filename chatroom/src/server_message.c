#include "server_message.h"
#include "server_conn_pool.h"
#include "server_dispatch.h"
#include "chat_common.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

static void format_timestamp(char* out, size_t out_sz) {
    if (out == NULL || out_sz == 0) return;

    time_t now = time(NULL);
    struct tm tm_now;
    memset(&tm_now, 0, sizeof(tm_now));
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
    localtime_r(&now, &tm_now);
#else
    struct tm* p = localtime(&now);
    if (p != NULL) tm_now = *p;
#endif
    strftime(out, out_sz, "%H:%M:%S", &tm_now);
}

static const char* skip_spaces(const char* s) {
    while (s != NULL && *s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    return s;
}

static int rate_limit_check_and_inc(ConnNode* node) {
    if (node == NULL) return 0;
    long now = (long)time(NULL);
    if (node->rate_window_sec != now) {
        node->rate_window_sec = now;
        node->rate_count = 0;
    }
    if (node->rate_count >= RATE_LIMIT_MAX_PER_WINDOW) {
        return 0;
    }
    node->rate_count++;
    return 1;
}

static ConnNode* find_by_client_id_locked(ConnPool* pool, int target_id) {
    ConnNode* curr = pool->head;
    while (curr != NULL) {
        if (!curr->is_closed && curr->client_id == target_id) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

int handle_single_msg(ConnPool* pool, ConnNode* node, int client_fd, char* msg) {
    if (node == NULL) return 1;

    msg[strcspn(msg, DELIMITER_STR)] = '\0';
    if (strlen(msg) == 0 || strspn(msg, " \t") == strlen(msg)) {
        return 0;
    }

    if (strcmp(msg, "PONG") == 0) {
        conn_pool_update_heartbeat(pool, client_fd, 1);
        return 0;
    }

    if (strcmp(msg, "exit") == 0) {
        char ts[16] = {0};
        format_timestamp(ts, sizeof(ts));

        printf("客户端[%d](%s)主动退出（输入exit指令）\n", node->client_id, node->nickname);
        mark_conn_closed(pool, client_fd);

        char leave_msg[BUF_SIZE] = {0};
        snprintf(leave_msg, sizeof(leave_msg), "[%s] [系统] %s(id=%d) 离开聊天室", ts,
                 node->nickname, node->client_id);
        server_dispatch_enqueue_broadcast(-1, leave_msg);

        conn_pool_del(pool, client_fd, 1);
        return 1;
    }

    if (msg[0] == '/') {
        if (strcmp(msg, "/status") == 0) {
            char status_buf[BUF_SIZE * 2] = {0};
            strncat(status_buf, "\n===== 【聊天室连接池状态】=====\n", sizeof(status_buf) - 1);
            char temp[64];
            pthread_mutex_lock(&pool->mutex);
            snprintf(temp, sizeof(temp), "最大支持在线人数：%d\n", pool->max_conn);
            strncat(status_buf, temp, sizeof(status_buf) - strlen(status_buf) - 1);
            snprintf(temp, sizeof(temp), "当前在线人数：%d\n", pool->cur_conn);
            strncat(status_buf, temp, sizeof(status_buf) - strlen(status_buf) - 1);
            strncat(status_buf, "============================\n",
                    sizeof(status_buf) - strlen(status_buf) - 1);
            pthread_mutex_unlock(&pool->mutex);

            send(client_fd, status_buf, strlen(status_buf), MSG_NOSIGNAL);
            printf("%s", status_buf);
            return 0;
        } else if (strcmp(msg, "/who") == 0 || strcmp(msg, "/list") == 0) {
            // 知识点6：连接管理 —— 返回当前在线连接列表
            char list_buf[BUF_SIZE * 2] = {0};
            strncat(list_buf, "\n===== 【在线连接列表】=====\n", sizeof(list_buf) - 1);

            pthread_mutex_lock(&pool->mutex);
            char temp[128];
            snprintf(temp, sizeof(temp), "当前在线人数：%d\n", pool->cur_conn);
            strncat(list_buf, temp, sizeof(list_buf) - strlen(list_buf) - 1);

            ConnNode* curr = pool->head;
            while (curr != NULL) {
                if (!curr->is_closed) {
                    snprintf(temp, sizeof(temp), "id: %d nick: %s\n", curr->client_id, curr->nickname);
                    strncat(list_buf, temp, sizeof(list_buf) - strlen(list_buf) - 1);
                }
                curr = curr->next;
            }
            pthread_mutex_unlock(&pool->mutex);

            strncat(list_buf, "==========================\n", sizeof(list_buf) - strlen(list_buf) - 1);
            send(client_fd, list_buf, strlen(list_buf), MSG_NOSIGNAL);
            return 0;
        } else if (strcmp(msg, "/help") == 0) {
            char help_msg[] =
                "\n===== 聊天室指令说明 =====\n"
                "/help - 查看所有指令\n"
                "/status - 查看在线人数及连接池状态\n"
                "/who - 查看在线连接列表\n"
                "/list - 查看在线连接列表（同/who）\n"
                "/nick <昵称> - 设置/修改昵称\n"
                "/pm <id> <内容> - 私聊指定用户\n"
                "exit - 退出聊天室\n"
                "其他内容 - 发送广播消息\n"
                "=========================\n";
            send(client_fd, help_msg, strlen(help_msg), MSG_NOSIGNAL);
            return 0;
        } else if (strncmp(msg, "/pm", 3) == 0) {
            const char* p = skip_spaces(msg + 3);
            if (p == NULL || *p == '\0') {
                char err_msg[] = "\n提示：用法 /pm <id> <内容>\n";
                send(client_fd, err_msg, strlen(err_msg), MSG_NOSIGNAL);
                return 0;
            }

            // 解析 target_id
            int target_id = 0;
            while (*p >= '0' && *p <= '9') {
                target_id = target_id * 10 + (*p - '0');
                p++;
            }
            p = skip_spaces(p);

            if (target_id <= 0 || p == NULL || *p == '\0') {
                char err_msg[] = "\n提示：用法 /pm <id> <内容>\n";
                send(client_fd, err_msg, strlen(err_msg), MSG_NOSIGNAL);
                return 0;
            }

            // 限流（私聊也属于用户消息）
            if (!rate_limit_check_and_inc(node)) {
                char warn[] = "\n提示：发送过快，请稍后再试（限流）\n";
                send(client_fd, warn, strlen(warn), MSG_NOSIGNAL);
                return 0;
            }

            char ts[16] = {0};
            format_timestamp(ts, sizeof(ts));

            int target_fd = -1;
            char target_nick[MAX_NICKNAME_LEN] = {0};
            pthread_mutex_lock(&pool->mutex);
            ConnNode* target = find_by_client_id_locked(pool, target_id);
            if (target != NULL) {
                target_fd = target->fd;
                snprintf(target_nick, sizeof(target_nick), "%s", target->nickname);
            }
            pthread_mutex_unlock(&pool->mutex);

            if (target_fd < 0) {
                char err_msg[BUF_SIZE] = {0};
                snprintf(err_msg, sizeof(err_msg), "\n提示：未找到 id=%d 的在线用户\n", target_id);
                send(client_fd, err_msg, strlen(err_msg), MSG_NOSIGNAL);
                return 0;
            }

            char to_target[BUF_SIZE * 2] = {0};
            snprintf(to_target, sizeof(to_target), "[%s] [私聊] %s(id=%d) -> 你：%s", ts,
                     node->nickname, node->client_id, p);
            send(target_fd, to_target, strlen(to_target), MSG_NOSIGNAL);
            send(target_fd, DELIMITER_STR, 1, MSG_NOSIGNAL);

            char to_self[BUF_SIZE * 2] = {0};
            snprintf(to_self, sizeof(to_self), "[%s] [私聊] 你 -> %s(id=%d)：%s", ts, target_nick,
                     target_id, p);
            send(client_fd, to_self, strlen(to_self), MSG_NOSIGNAL);
            send(client_fd, DELIMITER_STR, 1, MSG_NOSIGNAL);

                 // 服务端也打印私聊内容（便于演示/审计），但不广播给其他客户端
                 printf("[%s] [私聊] %s(id=%d) -> %s(id=%d)：%s\n", ts, node->nickname, node->client_id,
                     target_nick, target_id, p);
            return 0;
        } else if (strncmp(msg, "/nick", 5) == 0) {
            const char* p = skip_spaces(msg + 5);
            if (p == NULL || *p == '\0') {
                char err_msg[] = "\n提示：用法 /nick <昵称>\n";
                send(client_fd, err_msg, strlen(err_msg), MSG_NOSIGNAL);
                return 0;
            }

            char new_nick[MAX_NICKNAME_LEN] = {0};
            size_t i = 0;
            while (p[i] != '\0' && p[i] != '\n' && i < sizeof(new_nick) - 1) {
                new_nick[i] = p[i];
                i++;
            }
            new_nick[i] = '\0';

            pthread_mutex_lock(&pool->mutex);
            snprintf(node->nickname, sizeof(node->nickname), "%s", new_nick);
            pthread_mutex_unlock(&pool->mutex);

            char ok_msg[BUF_SIZE] = {0};
            snprintf(ok_msg, sizeof(ok_msg), "\n提示：昵称已设置为 %s\n", node->nickname);
            send(client_fd, ok_msg, strlen(ok_msg), MSG_NOSIGNAL);
            return 0;
        } else {
            char err_msg[] = "\n提示：未知指令，输入/help查看所有指令！\n";
            send(client_fd, err_msg, strlen(err_msg), MSG_NOSIGNAL);
            return 0;
        }
    }

    // 广播消息也做限流
    if (!rate_limit_check_and_inc(node)) {
        char warn[] = "\n提示：发送过快，请稍后再试（限流）\n";
        send(client_fd, warn, strlen(warn), MSG_NOSIGNAL);
        return 0;
    }

    // 统一服务端日志与广播给其他客户端的消息格式
    char ts[16] = {0};
    format_timestamp(ts, sizeof(ts));
    char out_msg[BUF_SIZE * 2] = {0};
    snprintf(out_msg, sizeof(out_msg), "[%s] %s(id=%d)：%s", ts, node->nickname, node->client_id, msg);

    printf("%s\n", out_msg);

    // 通过条件变量队列异步广播（知识点8：pthread_cond_t）
    server_dispatch_enqueue_broadcast(client_fd, out_msg);

    return 0;
}

int process_msg_buffer(ConnPool* pool, int client_fd, ConnNode* node) {
    if (node == NULL || node->is_closed) {
        return 1;
    }

    char* pos = NULL;
    while ((pos = strchr(node->msg_buf.data, MSG_DELIMITER)) != NULL) {
        int msg_len = (int)(pos - node->msg_buf.data + 1);
        char single_msg[BUF_SIZE] = {0};
        strncpy(single_msg, node->msg_buf.data, (size_t)msg_len);

        if (handle_single_msg(pool, node, client_fd, single_msg)) {
            return 1;
        }

        int remaining_len = node->msg_buf.len - msg_len;
        memmove(node->msg_buf.data, pos + 1, (size_t)remaining_len);
        memset(node->msg_buf.data + remaining_len, 0,
               sizeof(node->msg_buf.data) - (size_t)remaining_len);
        node->msg_buf.len = remaining_len;
    }

    return 0;
}
