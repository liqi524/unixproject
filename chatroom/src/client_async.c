#include "client.h"
#include "chat_common.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

static void print_prompt(void) {
    printf("请输入消息：");
    fflush(stdout);
}

static int handle_complete_line(ClientState* st, const char* line, int* history_mode, int* prompt_visible) {
    // 心跳：PING -> PONG
    if (strcmp(line, "PING") == 0) {
        const char pong[] = "PONG\n";
        send(st->fd, pong, strlen(pong), 0);
        return 0;
    }

    // 历史消息模式：由服务端用固定标记包裹
    if (strncmp(line, "[历史] ===== 最近消息开始 =====", 30) == 0) {
        if (history_mode) *history_mode = 1;
    } else if (strncmp(line, "[历史] ===== 最近消息结束 =====", 30) == 0) {
        if (history_mode) *history_mode = 0;
    }

    // 若当前提示符可见，先清掉这一行，避免“提示符单独占一行”
    if (prompt_visible && *prompt_visible) {
        printf("\r\033[K");
        *prompt_visible = 0;
    }

    // 普通文本：先换行输出（提示符由主循环在“本轮处理完成”后统一打印一次）
    printf("\n%s\n", line);
    return 1;
}

int client_run_async(ClientState* st) {
    // acc 用于处理 TCP 粘包/拆包：按 '\n' 分隔符拆成完整行
    char acc[BUF_SIZE * 4] = {0};
    int acc_len = 0;

    int prompt_visible = 0;
    int history_mode = 0;
    print_prompt();
    prompt_visible = 1;

    while (1) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(st->fd, &readfds);
        FD_SET(STDIN_FILENO, &readfds);

        int maxfd = st->fd > STDIN_FILENO ? st->fd : STDIN_FILENO;
        int rc = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("select");
            return -1;
        }

        // 1) socket 可读：接收并按行处理
        if (FD_ISSET(st->fd, &readfds)) {
            char buf[BUF_SIZE] = {0};
            ssize_t n = recv(st->fd, buf, sizeof(buf) - 1, 0);
            if (n <= 0) {
                printf("\n服务端已断开连接，程序退出\n");
                return 0;
            }

            int printed_any = 0;

            if (acc_len + (int)n >= (int)sizeof(acc) - 1) {
                // 理论上不会发生，防御：清空缓存避免溢出
                memset(acc, 0, sizeof(acc));
                acc_len = 0;
            }

            memcpy(acc + acc_len, buf, (size_t)n);
            acc_len += (int)n;
            acc[acc_len] = '\0';

            char* pos = NULL;
            while ((pos = strchr(acc, MSG_DELIMITER)) != NULL) {
                int line_len = (int)(pos - acc);
                char line[BUF_SIZE * 2] = {0};

                if (line_len > 0) {
                    int copy_len = line_len;
                    if (copy_len >= (int)sizeof(line)) {
                        copy_len = (int)sizeof(line) - 1;
                    }
                    memcpy(line, acc, (size_t)copy_len);
                    line[copy_len] = '\0';
                    if (handle_complete_line(st, line, &history_mode, &prompt_visible)) {
                        printed_any = 1;
                    }
                }

                int remaining = acc_len - (line_len + 1);
                memmove(acc, pos + 1, (size_t)remaining);
                acc_len = remaining;
                acc[acc_len] = '\0';
            }

            if (printed_any) {
                // prompt_visible 可能在 handle_complete_line 中已被清除
            }

            // 本轮 socket 输出处理完后，只打印一次提示符
            if (!history_mode && !prompt_visible) {
                print_prompt();
                prompt_visible = 1;
            }
        }

        // 2) stdin 可读：读取并发送
        if (FD_ISSET(STDIN_FILENO, &readfds)) {
            char input_buf[BUF_SIZE] = {0};
            char send_buf[SEND_BUF_SIZE] = {0};

            if (fgets(input_buf, sizeof(input_buf) - 2, stdin) == NULL) {
                // EOF（例如 Ctrl+D）
                break;
            }

            input_buf[strcspn(input_buf, "\n")] = '\0';

            if (strlen(input_buf) == 0) {
                print_prompt();
                prompt_visible = 1;
                continue;
            }

            if (strcmp(input_buf, "/clear") == 0) {
                // 本地清屏（不发送给服务端）
                printf("\033[2J\033[H");
                print_prompt();
                prompt_visible = 1;
                continue;
            }

            if (strcmp(input_buf, "exit") == 0) {
                // 保持旧行为：发送 "\nexit\n"
                snprintf(send_buf, sizeof(send_buf), "%c%s%c", MSG_DELIMITER, input_buf, MSG_DELIMITER);
                send(st->fd, send_buf, strlen(send_buf), 0);
                break;
            }

            if (strlen(input_buf) > CLIENT_MSG_LIMIT) {
                printf("\n提示：消息长度超过%d字符，已截断！\n", CLIENT_MSG_LIMIT);
                input_buf[CLIENT_MSG_LIMIT] = '\0';
            }

            snprintf(send_buf, sizeof(send_buf), "%s%c", input_buf, MSG_DELIMITER);
            send(st->fd, send_buf, strlen(send_buf), 0);

            // 指令（以/开头）通常会有多行响应：避免把提示符插到响应中间。
            // 非指令消息：立即打印提示符。
            if (input_buf[0] != '/') {
                print_prompt();
                prompt_visible = 1;
            } else {
                prompt_visible = 0;
            }
        }
    }

    return 0;
}
