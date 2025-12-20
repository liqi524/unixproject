#include "server_thread_pool.h"
#include "server_client_handler.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct ClientTask {
    int client_fd;
    struct ClientTask* next;
} ClientTask;

typedef struct ThreadPoolState {
    ConnPool* pool;
    volatile int* running;

    int worker_count;
    pthread_t* workers;

    pthread_mutex_t mutex;
    pthread_cond_t cond;

    ClientTask* head;
    ClientTask* tail;

    int started;
} ThreadPoolState;

static ThreadPoolState g_tp = {0};

static void free_task(ClientTask* t) {
    free(t);
}

static void* worker_thread(void* arg) {
    int worker_id = *(int*)arg;
    free(arg);

    while (1) {
        pthread_mutex_lock(&g_tp.mutex);
        while (*(g_tp.running) && g_tp.head == NULL) {
            pthread_cond_wait(&g_tp.cond, &g_tp.mutex);
        }

        if (!*(g_tp.running) && g_tp.head == NULL) {
            pthread_mutex_unlock(&g_tp.mutex);
            break;
        }

        ClientTask* task = g_tp.head;
        if (task != NULL) {
            g_tp.head = task->next;
            if (g_tp.head == NULL) {
                g_tp.tail = NULL;
            }
        }
        pthread_mutex_unlock(&g_tp.mutex);

        if (task == NULL) {
            continue;
        }

        // 线程池可观测日志：便于你在服务端终端确认 worker 复用
        printf("[worker-%d] handling client_fd=%d\n", worker_id, task->client_fd);

        client_handle_session(task->client_fd, g_tp.pool);

        free_task(task);
    }

    return NULL;
}

int server_thread_pool_init(ConnPool* pool, volatile int* running, int worker_count) {
    if (pool == NULL || running == NULL || worker_count <= 0) return -1;
    if (g_tp.started) return 0;

    memset(&g_tp, 0, sizeof(g_tp));
    g_tp.pool = pool;
    g_tp.running = running;
    g_tp.worker_count = worker_count;

    if (pthread_mutex_init(&g_tp.mutex, NULL) != 0) {
        perror("thread_pool mutex init failed");
        return -1;
    }
    if (pthread_cond_init(&g_tp.cond, NULL) != 0) {
        perror("thread_pool cond init failed");
        pthread_mutex_destroy(&g_tp.mutex);
        return -1;
    }

    g_tp.workers = (pthread_t*)calloc((size_t)worker_count, sizeof(pthread_t));
    if (g_tp.workers == NULL) {
        perror("thread_pool calloc failed");
        pthread_cond_destroy(&g_tp.cond);
        pthread_mutex_destroy(&g_tp.mutex);
        return -1;
    }

    g_tp.started = 1;

    for (int i = 0; i < worker_count; i++) {
        int* wid = (int*)malloc(sizeof(int));
        if (wid == NULL) {
            perror("thread_pool malloc failed");
            *running = 0;
            break;
        }
        *wid = i;
        if (pthread_create(&g_tp.workers[i], NULL, worker_thread, wid) != 0) {
            perror("thread_pool worker create failed");
            free(wid);
            *running = 0;
            break;
        }
    }

    return 0;
}

int server_thread_pool_enqueue_client(int client_fd) {
    if (!g_tp.started) return -1;

    ClientTask* task = (ClientTask*)malloc(sizeof(ClientTask));
    if (task == NULL) return -1;
    task->client_fd = client_fd;
    task->next = NULL;

    pthread_mutex_lock(&g_tp.mutex);
    if (g_tp.tail == NULL) {
        g_tp.head = task;
        g_tp.tail = task;
    } else {
        g_tp.tail->next = task;
        g_tp.tail = task;
    }
    pthread_cond_signal(&g_tp.cond);
    pthread_mutex_unlock(&g_tp.mutex);

    return 0;
}

void server_thread_pool_shutdown(void) {
    if (!g_tp.started) return;

    pthread_mutex_lock(&g_tp.mutex);
    pthread_cond_broadcast(&g_tp.cond);
    pthread_mutex_unlock(&g_tp.mutex);

    for (int i = 0; i < g_tp.worker_count; i++) {
        if (g_tp.workers[i]) {
            pthread_join(g_tp.workers[i], NULL);
        }
    }

    pthread_mutex_lock(&g_tp.mutex);
    ClientTask* curr = g_tp.head;
    while (curr != NULL) {
        ClientTask* next = curr->next;
        free_task(curr);
        curr = next;
    }
    g_tp.head = NULL;
    g_tp.tail = NULL;
    pthread_mutex_unlock(&g_tp.mutex);

    free(g_tp.workers);
    g_tp.workers = NULL;

    pthread_cond_destroy(&g_tp.cond);
    pthread_mutex_destroy(&g_tp.mutex);

    g_tp.started = 0;
}
