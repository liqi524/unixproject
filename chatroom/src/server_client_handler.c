#include "server_client_handler.h"
#include "server_conn_pool.h"
#include "server_dispatch.h"
#include "server_message.h"
#include "server_types.h"
#include "chat_common.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

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

int client_handle_session(int client_fd, ConnPool* pool) {
    ConnNode* node = conn_pool_get_node(pool, client_fd);
    int ret = 0;

    if (node == NULL) {
        return 0;
    }

    struct timeval tv = {1, 0};
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    while (1) {
        if (is_conn_closed(pool, client_fd) || node == NULL) {
            ret = 0;
            goto cleanup;
        }

        char recv_buf[BUF_SIZE] = {0};
        ssize_t recv_len = recv(client_fd, recv_buf, sizeof(recv_buf) - 1, 0);

        if (recv_len <= 0) {
            if (recv_len < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }

            char ts[16] = {0};
            format_timestamp(ts, sizeof(ts));

            printf("客户端[%d](%s)异常退出（TCP连接关闭，如直接关闭终端）\n", node->client_id,
                   node->nickname);

            mark_conn_closed(pool, client_fd);

            char leave_msg[BUF_SIZE] = {0};
            snprintf(leave_msg, sizeof(leave_msg), "[%s] [系统] %s(id=%d) 离开聊天室", ts,
                     node->nickname, node->client_id);
            server_dispatch_enqueue_broadcast(-1, leave_msg);

            conn_pool_del(pool, client_fd, 1);
            ret = 0;
            goto cleanup;
        }

        if (node->msg_buf.len + (int)recv_len >= (int)sizeof(node->msg_buf.data)) {
            msg_buf_clear(&node->msg_buf);
            char err_msg[] = "\n提示：消息缓冲区溢出，已清空！\n";
            send(client_fd, err_msg, strlen(err_msg), MSG_NOSIGNAL);
            continue;
        }

        strncat(node->msg_buf.data, recv_buf, (size_t)recv_len);
        node->msg_buf.len += (int)recv_len;

        if (process_msg_buffer(pool, client_fd, node)) {
            ret = 0;
            goto cleanup;
        }
    }

cleanup:
    conn_pool_put_node(pool, node);
    return ret;
}
