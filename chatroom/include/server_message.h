#pragma once

#include "server_types.h"

int handle_single_msg(ConnPool* pool, ConnNode* node, int client_fd, char* msg);
int process_msg_buffer(ConnPool* pool, int client_fd, ConnNode* node);
