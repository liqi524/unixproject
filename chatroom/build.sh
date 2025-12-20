#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

CFLAGS=(-Wall -Wextra -O2 -pthread -Iinclude)

CLIENT_SRCS=(
  client.c
  src/client_async.c
)

SERVER_SRCS=(
  server.c
  src/server_conn_pool.c
  src/server_message.c
  src/server_dispatch.c
  src/server_thread_pool.c
  src/server_heartbeat.c
  src/server_client_handler.c
)

usage() {
  cat <<'EOF'
Usage:
  ./build.sh        # build server and client
  ./build.sh clean  # remove build outputs
EOF
}

if [[ ${1:-} == "-h" || ${1:-} == "--help" ]]; then
  usage
  exit 0
fi

if [[ ${1:-} == "clean" ]]; then
  rm -f server client
  echo "Cleaned: server client"
  exit 0
fi

if ! command -v gcc >/dev/null 2>&1; then
  echo "Error: gcc not found. Install build tools first:" >&2
  echo "  sudo apt update && sudo apt install -y build-essential" >&2
  exit 1
fi

set -x

gcc "${CFLAGS[@]}" -o server "${SERVER_SRCS[@]}"

gcc "${CFLAGS[@]}" -o client "${CLIENT_SRCS[@]}"

set +x

echo "Build OK: ./server ./client"
