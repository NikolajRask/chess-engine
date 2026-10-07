#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ ! -x "$ROOT/build/server/chess-server" ]]; then
  echo "Build the server first:"
  echo "  cmake -S \"$ROOT\" -B \"$ROOT/build\" -DCMAKE_BUILD_TYPE=Release"
  echo "  cmake --build \"$ROOT/build\" --target chess-server"
  exit 1
fi

"$ROOT/build/server/chess-server" &
SERVER_PID=$!
cleanup() {
  kill "$SERVER_PID" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

echo "API:  http://127.0.0.1:8080/health"
cd "$ROOT/web"
echo "Web:  http://127.0.0.1:5173/"
npm run dev -- --host 127.0.0.1
