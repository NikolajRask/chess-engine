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

## NNUE eval (optional)

Classical eval is the default (with a pawn-structure hash). When `nets/default.nnue` is present, the UCI engine still loads it, but NNUE stays off until you enable it (`setoption name UseNNUE value true`). `EvalFile` selects the net path.

A small starter net is included under `nets/`. Retrain for real Elo gains (more positions / deeper labels).

**Recommended: interruptible overnight run** (Ctrl+C anytime — data is flushed, training checkpoints each epoch):

```bash
cmake --build build --target chess-uci
pip install -r scripts/nnue/requirements-nnue.txt

# Keeps Mac awake; default ~8h (≈70% labeling, ≈30% train)
# Defaults: 1M positions @ depth 7 → data/nnue_positions_d7.csv
# On MacBook Air M4, expect multiple nights of labeling (resume/append is fine).
caffeinate -dims ./scripts/nnue/overnight.sh
# or: caffeinate -dims ./scripts/nnue/overnight.sh --hours 8 --positions 1000000 --depth 7
```

Use a **fresh CSV when changing label depth** (do not mix depths). The older `data/nnue_positions.csv` (depth 4) is left alone; overnight writes `data/nnue_positions_d7.csv`.

Manual / resumable pieces:

```bash
# Label (append + Ctrl+C safe)
python3 scripts/nnue/generate_data.py --engine ./build/uci/chess-uci --positions 1000000 --depth 7 --append

# Train (resume + Ctrl+C exports latest net; optional time cap)
python3 scripts/nnue/train.py --data data/nnue_positions_d7.csv --epochs 200 --resume \
  --checkpoint data/nnue_checkpoint_d7.pt --max-hours 3 --out nets/default.nnue
```

Then restart `chess-uci` (or `setoption name EvalFile value nets/default.nnue`).

## Estimate Elo

Play a gauntlet against Stockfish at calibrated `UCI_Elo` anchors (CCRL-anchored scale, not Chess.com/FIDE).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target chess-uci
pip install -r scripts/requirements-elo.txt
brew install stockfish   # if needed

# ~10 minutes, rough estimate
python3 scripts/elo_estimate.py --quick

# Default: 80 games, 1s/move (~20–40 minutes)
python3 scripts/elo_estimate.py
```

Optional flags: `--ours`, `--stockfish`, `--games`, `--movetime`, `--tc 10+0.1`, `--anchors 2400,2600,2800`.

Set `VITE_API_URL` in `web/.env` if the API is not on `http://localhost:8080`.

## API

- `GET /health` → `{ "ok": true }`
- `POST /api/bestmove` → `{ "fen": "...", "timeMs": 2000, "threads": 1, "skillLevel": 20, "useBook": true }`
  - Response: `{ "bestmove": "e2e4", "scoreCp": 35, "scoreMate": null, "depth": 10, "nodes": 123456, "timeMs": 1980, "nps": 62300, "pv": ["e2e4", "e7e5"], "fromBook": false }`
