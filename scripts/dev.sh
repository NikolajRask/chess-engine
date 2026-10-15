#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

if [[ ! -x "$ROOT/build/server/chess-server" ]]; then
  echo "Build the server first:"
  echo "  cmake -S \"$ROOT\" -B \"$ROOT/build\" -DCMAKE_BUILD_TYPE=Release"
  echo "  cmake --build \"$ROOT/build\" --target chess-server"
  exit 1
fi

# Avoid duplicate listeners (overlapping old servers cause intermittent Failed to fetch).
if command -v lsof >/dev/null 2>&1; then
  lsof -tiTCP:8080 -sTCP:LISTEN 2>/dev/null | xargs kill 2>/dev/null || true
  sleep 0.2
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
