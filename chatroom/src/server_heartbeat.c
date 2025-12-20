#include "server_heartbeat.h"
#include "chat_common.h"
#include "server_dispatch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct PendingLeave {
    int client_id;
    char nickname[MAX_NICKNAME_LEN];
} PendingLeave;

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

void conn_pool_heartbeat_check(ConnPool* pool) {
    if (pool == NULL) return;

    PendingLeave pending[MAX_CONN];
    int pending_n = 0;

    pthread_mutex_lock(&pool->mutex);
    ConnNode *prev = NULL, *curr = pool->head;
    while (curr != NULL) {
        char ping[] = "PING\n";
        ssize_t send_len = send(curr->fd, ping, strlen(ping), MSG_NOSIGNAL);

        if (send_len <= 0 || curr->heartbeat_count >= HEARTBEAT_TIMEOUT) {
            if (send_len <= 0) {
                printf("客户端[%d]心跳包发送失败，判定为异常退出（网络断开/进程卡死）\n", curr->client_id);
            } else {
                printf("客户端[%d]心跳超时（%d次未回复），判定为异常退出\n", curr->client_id, HEARTBEAT_TIMEOUT);
            }

            ConnNode* tmp = curr;
            int removed_id = tmp->client_id; // 避免 free 后再读

            if (pending_n < MAX_CONN) {
                pending[pending_n].client_id = tmp->client_id;
                snprintf(pending[pending_n].nickname, sizeof(pending[pending_n].nickname), "%s",
                         tmp->nickname);
                pending_n++;
            }

            if (prev == NULL) {
                pool->head = curr->next;
            } else {
                prev->next = curr->next;
            }

            // 先推进 curr，避免后续 tmp 可能被延迟释放影响遍历
            curr = (prev == NULL) ? pool->head : prev->next;

            tmp->is_closed = 1;
            close(tmp->fd);
            tmp->removed = 1;
            pool->cur_conn--;
            printf("客户端[%d]已从连接池移除，当前在线：%d人\n", removed_id, pool->cur_conn);

            // 若仍有线程持有 tmp 指针，延迟到 put_node() 再释放
            if (tmp->refcount == 0) {
                free(tmp);
            }
        } else {
            curr->heartbeat_count++;
            prev = curr;
            curr = curr->next;
        }
    }
    pthread_mutex_unlock(&pool->mutex);

    // 在释放 pool->mutex 之后再发系统消息，避免与 dispatch 线程的锁顺序冲突
    for (int i = 0; i < pending_n; i++) {
        char ts[16] = {0};
        format_timestamp(ts, sizeof(ts));
        char leave_msg[BUF_SIZE] = {0};
        snprintf(leave_msg, sizeof(leave_msg), "[%s] [系统] %s(id=%d) 离开聊天室", ts,
                 pending[i].nickname, pending[i].client_id);
        server_dispatch_enqueue_broadcast(-1, leave_msg);
    }
}

void* heartbeat_thread(void* arg) {
    HeartbeatArgs* hb = (HeartbeatArgs*)arg;
    ConnPool* pool = hb->pool;
    while (*(hb->running)) {
        sleep(HEARTBEAT_INTERVAL);
        if (!*(hb->running)) {
            break;
        }
        conn_pool_heartbeat_check(pool);
    }
    return NULL;
}
