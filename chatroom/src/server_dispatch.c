#include "server_dispatch.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define HISTORY_SIZE 20

typedef struct BroadcastTask {
    int sender_fd;
    char* msg;
    struct BroadcastTask* next;
} BroadcastTask;

typedef struct DispatchState {
    ConnPool* pool;
    volatile int* running;

    pthread_t tid;
    pthread_mutex_t mutex;
    pthread_cond_t cond;

    BroadcastTask* head;
    BroadcastTask* tail;

    pthread_mutex_t hist_mutex;
    char* history[HISTORY_SIZE];
    int hist_count;
    int hist_head;

    int started;
} DispatchState;

static DispatchState g_ds = {0};

static void history_add(const char* msg) {
    if (msg == NULL) return;

    pthread_mutex_lock(&g_ds.hist_mutex);

    // 覆盖最旧
    if (g_ds.hist_count == HISTORY_SIZE) {
        free(g_ds.history[g_ds.hist_head]);
        g_ds.history[g_ds.hist_head] = NULL;
        g_ds.hist_head = (g_ds.hist_head + 1) % HISTORY_SIZE;
        g_ds.hist_count--;
    }

    int tail = (g_ds.hist_head + g_ds.hist_count) % HISTORY_SIZE;
    size_t n = strlen(msg) + 1;
    char* copy = (char*)malloc(n);
    if (copy != NULL) {
        memcpy(copy, msg, n);
        g_ds.history[tail] = copy;
        g_ds.hist_count++;
    }

    pthread_mutex_unlock(&g_ds.hist_mutex);
}

void server_dispatch_send_history(int client_fd) {
    if (!g_ds.started || client_fd < 0) return;

    pthread_mutex_lock(&g_ds.hist_mutex);
    int count = g_ds.hist_count;
    int idx = g_ds.hist_head;
    char delimiter = MSG_DELIMITER;

    if (count <= 0) {
        pthread_mutex_unlock(&g_ds.hist_mutex);
        return;
    }

    // 给用户明确标识“以下为历史消息”
    const char* begin = "[历史] ===== 最近消息开始 =====";
    send(client_fd, begin, strlen(begin), MSG_NOSIGNAL);
    send(client_fd, &delimiter, 1, MSG_NOSIGNAL);

    for (int i = 0; i < count; i++) {
        const char* msg = g_ds.history[idx];
        if (msg != NULL) {
            char line[BUF_SIZE * 2] = {0};
            snprintf(line, sizeof(line), "[历史] %s", msg);
            send(client_fd, line, strlen(line), MSG_NOSIGNAL);
            send(client_fd, &delimiter, 1, MSG_NOSIGNAL);
        }
        idx = (idx + 1) % HISTORY_SIZE;
    }

    const char* end = "[历史] ===== 最近消息结束 =====";
    send(client_fd, end, strlen(end), MSG_NOSIGNAL);
    send(client_fd, &delimiter, 1, MSG_NOSIGNAL);

    pthread_mutex_unlock(&g_ds.hist_mutex);
}

static void free_task(BroadcastTask* t) {
    if (t == NULL) return;
    free(t->msg);
    free(t);
}

static void* dispatch_thread(void* arg) {
    (void)arg;

    while (1) {
        pthread_mutex_lock(&g_ds.mutex);
        while (*(g_ds.running) && g_ds.head == NULL) {
            pthread_cond_wait(&g_ds.cond, &g_ds.mutex);
        }

        if (!*(g_ds.running) && g_ds.head == NULL) {
            pthread_mutex_unlock(&g_ds.mutex);
            break;
        }

        BroadcastTask* task = g_ds.head;
        if (task != NULL) {
            g_ds.head = task->next;
            if (g_ds.head == NULL) {
                g_ds.tail = NULL;
            }
        }
        pthread_mutex_unlock(&g_ds.mutex);

        if (task == NULL) {
            continue;
        }

        // 记录历史（包括系统消息与聊天广播）
        history_add(task->msg);

        // 广播：发送给除发送者以外的所有在线连接
        pthread_mutex_lock(&g_ds.pool->mutex);
        ConnNode* curr = g_ds.pool->head;
        char delimiter = MSG_DELIMITER;
        while (curr != NULL) {
            if (curr->fd != task->sender_fd && !curr->is_closed) {
                send(curr->fd, task->msg, strlen(task->msg), MSG_NOSIGNAL);
                send(curr->fd, &delimiter, 1, MSG_NOSIGNAL);
            }
            curr = curr->next;
        }
        pthread_mutex_unlock(&g_ds.pool->mutex);

        free_task(task);
    }

    return NULL;
}

int server_dispatch_init(ConnPool* pool, volatile int* running) {
    if (pool == NULL || running == NULL) return -1;
    if (g_ds.started) return 0;

    memset(&g_ds, 0, sizeof(g_ds));
    g_ds.pool = pool;
    g_ds.running = running;

    if (pthread_mutex_init(&g_ds.mutex, NULL) != 0) {
        perror("dispatch mutex init failed");
        return -1;
    }
    if (pthread_cond_init(&g_ds.cond, NULL) != 0) {
        perror("dispatch cond init failed");
        pthread_mutex_destroy(&g_ds.mutex);
        return -1;
    }

    if (pthread_mutex_init(&g_ds.hist_mutex, NULL) != 0) {
        perror("dispatch hist mutex init failed");
        pthread_cond_destroy(&g_ds.cond);
        pthread_mutex_destroy(&g_ds.mutex);
        g_ds.started = 0;
        return -1;
    }

    for (int i = 0; i < HISTORY_SIZE; i++) {
        g_ds.history[i] = NULL;
    }
    g_ds.hist_count = 0;
    g_ds.hist_head = 0;

    g_ds.started = 1;
    if (pthread_create(&g_ds.tid, NULL, dispatch_thread, NULL) != 0) {
        perror("dispatch thread create failed");
        pthread_cond_destroy(&g_ds.cond);
        pthread_mutex_destroy(&g_ds.mutex);
        g_ds.started = 0;
        return -1;
    }

    return 0;
}

int server_dispatch_enqueue_broadcast(int sender_fd, const char* msg) {
    if (!g_ds.started || msg == NULL) return -1;

    BroadcastTask* task = (BroadcastTask*)malloc(sizeof(BroadcastTask));
    if (task == NULL) return -1;
    memset(task, 0, sizeof(*task));

    task->sender_fd = sender_fd;
    size_t msg_len = strlen(msg) + 1;
    task->msg = (char*)malloc(msg_len);
    if (task->msg == NULL) {
        free(task);
        return -1;
    }
    memcpy(task->msg, msg, msg_len);

    pthread_mutex_lock(&g_ds.mutex);
    if (g_ds.tail == NULL) {
        g_ds.head = task;
        g_ds.tail = task;
    } else {
        g_ds.tail->next = task;
        g_ds.tail = task;
    }
    pthread_cond_signal(&g_ds.cond);
    pthread_mutex_unlock(&g_ds.mutex);

    return 0;
}

void server_dispatch_shutdown(void) {
    if (!g_ds.started) return;

    // 唤醒线程退出
    pthread_mutex_lock(&g_ds.mutex);
    pthread_cond_broadcast(&g_ds.cond);
    pthread_mutex_unlock(&g_ds.mutex);

    pthread_join(g_ds.tid, NULL);

    // 清空历史
    pthread_mutex_lock(&g_ds.hist_mutex);
    for (int i = 0; i < HISTORY_SIZE; i++) {
        free(g_ds.history[i]);
        g_ds.history[i] = NULL;
    }
    g_ds.hist_count = 0;
    g_ds.hist_head = 0;
    pthread_mutex_unlock(&g_ds.hist_mutex);

    // 清空队列
    pthread_mutex_lock(&g_ds.mutex);
    BroadcastTask* curr = g_ds.head;
    while (curr != NULL) {
        BroadcastTask* next = curr->next;
        free_task(curr);
        curr = next;
    }
    g_ds.head = NULL;
    g_ds.tail = NULL;
    pthread_mutex_unlock(&g_ds.mutex);

    pthread_cond_destroy(&g_ds.cond);
    pthread_mutex_destroy(&g_ds.mutex);
    pthread_mutex_destroy(&g_ds.hist_mutex);
    g_ds.started = 0;
}
