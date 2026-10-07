# Chess AI

Custom C++ chess engine with a React web UI. The browser sends the current FEN to the native server; the engine searches for the best move and returns UCI.

## Prerequisites

- CMake 3.20+
- C++20 compiler (Clang or GCC)
- Node.js 18+

## Build and run (engine + server)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target chess-server
```

### One terminal (recommended)

```bash
cd web && npm install   # first time only
./scripts/dev.sh
```

Opens the API on port **8080** and the site on **5173**. Use **http://127.0.0.1:5173/** in the browser.

### Two terminals

The API process runs until you stop it (`Ctrl+C`). If you run `./build/server/chess-server` in the same shell as `npm run dev`, the frontend **never starts** because the server blocks that terminal.

**Terminal 1:**

```bash
./build/server/chess-server
```

**Terminal 2:**

```bash
cd web && npm run dev
```

Do not stop Terminal 1 while you play; that kills the engine API.

## Run tests (perft)

```bash
cmake --build build --target engine_tests
ctest --test-dir build --output-on-failure
```

Set `VITE_API_URL` in `web/.env` if the API is not on `http://localhost:8080`.

## API

- `GET /health` → `{ "ok": true }`
- `POST /api/bestmove` → `{ "fen": "...", "timeMs": 2000, "maxDepth": 12 }`
  - Response: `{ "bestmove": "e2e4", "scoreCp": 35, "depth": 10 }`
