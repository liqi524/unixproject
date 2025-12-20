#pragma once

#include "server_types.h"

typedef struct HeartbeatArgs {
	ConnPool* pool;
	volatile int* running;
} HeartbeatArgs;

void conn_pool_heartbeat_check(ConnPool* pool);
void* heartbeat_thread(void* arg);
