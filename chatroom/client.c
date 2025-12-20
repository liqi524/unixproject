#include "chat_common.h"
#include "client.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main() {
    ClientState st = {0};

    st.fd = socket(AF_INET, SOCK_STREAM, 0);
    if (st.fd == -1) {
        perror("套接字创建失败");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    if (connect(st.fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("服务端连接失败");
        close(st.fd);
        exit(EXIT_FAILURE);
    }

    printf("=== 成功连接到聊天室 ===\n");
    printf("提示：输入/help查看指令，输入exit退出，消息长度限制100字符\n");

    // 知识点5：异步网络（select 监听 socket + stdin）
    client_run_async(&st);

    close(st.fd);
    printf("\n已退出聊天室\n");
    return 0;
}
