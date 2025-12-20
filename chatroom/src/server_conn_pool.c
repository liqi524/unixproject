#include "server_conn_pool.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void msg_buf_init(MsgBuffer* buf) {
    memset(buf->data, 0, sizeof(buf->data));
    buf->len = 0;
}

void msg_buf_clear(MsgBuffer* buf) {
    memset(buf->data, 0, sizeof(buf->data));
    buf->len = 0;
}

ConnPool* conn_pool_init(int max_conn) {
    ConnPool* pool = (ConnPool*)malloc(sizeof(ConnPool));
    if (pool == NULL) {
        perror("连接池内存分配失败");
        return NULL;
    }
    pool->head = NULL;
    pool->max_conn = max_conn;
    pool->cur_conn = 0;
    pool->next_client_id = 1;
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        perror("连接池锁初始化失败");
        free(pool);
        return NULL;
    }
    return pool;
}

int conn_pool_add(ConnPool* pool, int fd) {
    if (pool == NULL || fd < 0) return -1;

    pthread_mutex_lock(&pool->mutex);
    if (pool->cur_conn >= pool->max_conn) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    ConnNode* node = (ConnNode*)malloc(sizeof(ConnNode));
    if (node == NULL) {
        pthread_mutex_unlock(&pool->mutex);
        perror("连接池节点内存分配失败");
        return -1;
    }

    node->fd = fd;
    node->client_id = pool->next_client_id++;
    snprintf(node->nickname, sizeof(node->nickname), "用户%d", node->client_id);
    node->rate_window_sec = 0;
    node->rate_count = 0;
    node->heartbeat_count = 0;
    node->is_closed = 0;
    node->refcount = 0;
    node->removed = 0;
    msg_buf_init(&node->msg_buf);
    node->next = pool->head;
    pool->head = node;
    pool->cur_conn++;

    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

int conn_pool_del(ConnPool* pool, int fd, int print_log) {
    if (pool == NULL || fd < 0) return -1;

    pthread_mutex_lock(&pool->mutex);
    ConnNode *prev = NULL, *curr = pool->head;
    while (curr != NULL && curr->fd != fd) {
        prev = curr;
        curr = curr->next;
    }

    if (curr == NULL) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    int removed_id = curr->client_id;
    curr->is_closed = 1;

    if (prev == NULL) {
        pool->head = curr->next;
    } else {
        prev->next = curr->next;
    }

    close(curr->fd);
    curr->removed = 1;
    pool->cur_conn--;

    if (print_log) {
        printf("客户端[%d]已从连接池移除，当前在线：%d人\n", removed_id, pool->cur_conn);
    }

    // 若仍有线程持有该节点指针，延迟释放到 put_node()
    if (curr->refcount == 0) {
        free(curr);
    }

    pthread_mutex_unlock(&pool->mutex);
    return 0;
}

ConnNode* conn_pool_get_node(ConnPool* pool, int fd) {
    if (pool == NULL || fd < 0) return NULL;

    pthread_mutex_lock(&pool->mutex);
    ConnNode* curr = pool->head;
    while (curr != NULL && curr->fd != fd) {
        curr = curr->next;
    }
    if (curr != NULL) {
        curr->refcount++;
    }
    pthread_mutex_unlock(&pool->mutex);
    return curr;
}

void conn_pool_put_node(ConnPool* pool, ConnNode* node) {
    if (pool == NULL || node == NULL) return;

    pthread_mutex_lock(&pool->mutex);
    if (node->refcount > 0) {
        node->refcount--;
    }

    int can_free = (node->refcount == 0 && node->removed);
    pthread_mutex_unlock(&pool->mutex);

    if (can_free) {
        free(node);
    }
}

void conn_pool_update_heartbeat(ConnPool* pool, int fd, int reset) {
    if (pool == NULL || fd < 0) return;

    pthread_mutex_lock(&pool->mutex);
    ConnNode* curr = pool->head;
    while (curr != NULL) {
        if (curr->fd == fd) {
            curr->heartbeat_count = reset ? 0 : curr->heartbeat_count + 1;
            break;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&pool->mutex);
}

void mark_conn_closed(ConnPool* pool, int fd) {
    if (pool == NULL || fd < 0) return;

    pthread_mutex_lock(&pool->mutex);
    ConnNode* curr = pool->head;
    while (curr != NULL) {
        if (curr->fd == fd) {
            curr->is_closed = 1;
            break;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&pool->mutex);
}

int is_conn_closed(ConnPool* pool, int fd) {
    if (pool == NULL || fd < 0) return 1;

    pthread_mutex_lock(&pool->mutex);
    ConnNode* curr = pool->head;
    int closed = 1;
    while (curr != NULL) {
        if (curr->fd == fd) {
            closed = curr->is_closed;
            break;
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&pool->mutex);
    return closed;
}

void conn_pool_close_all(ConnPool* pool, int print_log) {
    if (pool == NULL) return;

    pthread_mutex_lock(&pool->mutex);
    ConnNode* curr = pool->head;
    while (curr != NULL) {
        if (!curr->is_closed) {
            curr->is_closed = 1;
            close(curr->fd);
            if (print_log) {
                printf("客户端[%d]已关闭（服务端退出）\n", curr->fd);
            }
        }
        curr = curr->next;
    }
    pthread_mutex_unlock(&pool->mutex);
}
