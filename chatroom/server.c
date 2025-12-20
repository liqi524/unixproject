#include "chat_common.h"
#include "server_client_handler.h"
#include "server_conn_pool.h"
#include "server_dispatch.h"
#include "server_heartbeat.h"
#include "server_thread_pool.h"
#include "server_types.h"

#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
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

typedef struct ServerCtlArgs {
    volatile int* running;
    int server_fd;
} ServerCtlArgs;

static volatile int* g_running_ptr = NULL;
static volatile int g_listen_fd = -1;

static void on_signal(int sig) {
    (void)sig;
    if (g_running_ptr != NULL) {
        *g_running_ptr = 0;
    }
    if (g_listen_fd >= 0) {
        shutdown((int)g_listen_fd, SHUT_RDWR);
    }
}

static void* server_console_thread(void* arg) {
    ServerCtlArgs* ctl = (ServerCtlArgs*)arg;
    char line[64] = {0};
    while (*(ctl->running)) {
        if (fgets(line, sizeof(line), stdin) == NULL) {
            // stdin 关闭/EOF：不强制退出服务端
            break;
        }
        line[strcspn(line, "\n")] = '\0';
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0) {
            *(ctl->running) = 0;
            shutdown(ctl->server_fd, SHUT_RDWR);
            break;
        }
        memset(line, 0, sizeof(line));
    }
    return NULL;
}

int main() {
    volatile int running = 1;
    const int worker_count = 4;
    ConnPool* pool = conn_pool_init(MAX_CONN);
    if (pool == NULL) {
        exit(EXIT_FAILURE);
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("套接字创建失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("地址绑定失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) == -1) {
        perror("端口监听失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    // 设置信号处理：Ctrl+C 或 kill 触发退出
    g_running_ptr = &running;
    g_listen_fd = server_fd;
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    printf("=== 带线程安全连接池的聊天室服务端启动 ===\n");
    printf("监听端口：%d | 最大在线人数：%d\n", PORT, MAX_CONN);
    printf("消息分隔符：%c | 客户端可输入 /help 查看指令说明\n\n", MSG_DELIMITER);
    printf("服务端控制：输入 exit 或 quit 关闭服务端\n\n");

    // 启动线程池（知识点9：线程池）
    if (server_thread_pool_init(pool, &running, worker_count) != 0) {
        fprintf(stderr, "线程池启动失败\n");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    printf("线程池已启动：worker_count=%d（不会为每个客户端创建新线程）\n\n", worker_count);

    // 启动消息调度线程（条件变量队列）
    if (server_dispatch_init(pool, &running) != 0) {
        fprintf(stderr, "消息调度线程启动失败\n");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    pthread_t heartbeat_tid;
    HeartbeatArgs hb_args = {.pool = pool, .running = &running};
    if (pthread_create(&heartbeat_tid, NULL, heartbeat_thread, &hb_args) != 0) {
        perror("心跳线程创建失败");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    pthread_detach(heartbeat_tid);

    // 控制台线程：读取exit/quit命令
    pthread_t console_tid;
    ServerCtlArgs ctl_args = {.running = &running, .server_fd = server_fd};
    if (pthread_create(&console_tid, NULL, server_console_thread, &ctl_args) == 0) {
        pthread_detach(console_tid);
    }

    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    while (running) {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (!running) {
                break;
            }
            perror("客户端连接接收失败");
            continue;
        }

        if (conn_pool_add(pool, client_fd) == -1) {
            char* full_msg = "聊天室已满，无法连接\n";
            send(client_fd, full_msg, strlen(full_msg), MSG_NOSIGNAL);
            close(client_fd);
            printf("客户端[%s:%d]连接被拒绝：连接池已满\n", inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port));
            continue;
        }

        // 使用线程池投递任务：worker 会调用 client_handle_session()
        if (server_thread_pool_enqueue_client(client_fd) != 0) {
            perror("线程池投递任务失败");
            conn_pool_del(pool, client_fd, 0);
            continue;
        }

        // 发送加入聊天室系统消息（对所有在线用户可见，包括新用户）
        ConnNode* node = conn_pool_get_node(pool, client_fd);
        if (node != NULL) {
            // 先下发历史消息给新用户
            server_dispatch_send_history(client_fd);

            char ts[16] = {0};
            format_timestamp(ts, sizeof(ts));
            char join_msg[BUF_SIZE] = {0};
            snprintf(join_msg, sizeof(join_msg), "[%s] [系统] %s(id=%d) 加入聊天室", ts,
                     node->nickname, node->client_id);
            server_dispatch_enqueue_broadcast(-1, join_msg);

            printf("客户端[id=%d nick=%s %s:%d]已连接，当前在线：%d人\n", node->client_id,
                   node->nickname, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),
                   pool->cur_conn);
            conn_pool_put_node(pool, node);
        } else {
            printf("客户端[%s:%d]已连接，当前在线：%d人\n", inet_ntoa(client_addr.sin_addr),
                   ntohs(client_addr.sin_port), pool->cur_conn);
        }
    }

    // 退出流程：停止接收新连接、关闭所有客户端
    running = 0;
    shutdown(server_fd, SHUT_RDWR);
    close(server_fd);

    // 先关闭所有客户端 socket，解除 worker 中 recv 阻塞
    conn_pool_close_all(pool, 1);

    // 停止调度线程（会 join），确保无悬挂广播
    server_dispatch_shutdown();

    // 停止线程池并等待 worker 退出
    server_thread_pool_shutdown();

    pthread_mutex_destroy(&pool->mutex);
    free(pool);
    return 0;
}
