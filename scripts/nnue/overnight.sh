#!/usr/bin/env bash
# Overnight NNUE pipeline: generate labels, then train.
# Safe to stop anytime with Ctrl+C — CSV is flushed; training checkpoints each epoch.
#
# Usage (from repo root):
#   caffeinate -dims ./scripts/nnue/overnight.sh
#   caffeinate -dims ./scripts/nnue/overnight.sh --hours 8
#   ./scripts/nnue/overnight.sh --positions 1000000 --depth 7

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

HOURS=8
POSITIONS=1000000
DEPTH=7
TRAIN_EPOCHS=200
ENGINE="${ENGINE:-./build/uci/chess-uci}"
DATA="data/nnue_positions_d7.csv"
OUT="nets/default.nnue"
CKPT="data/nnue_checkpoint_d7.pt"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --hours) HOURS="$2"; shift 2 ;;
    --positions) POSITIONS="$2"; shift 2 ;;
    --depth) DEPTH="$2"; shift 2 ;;
    --train-epochs) TRAIN_EPOCHS="$2"; shift 2 ;;
    --engine) ENGINE="$2"; shift 2 ;;
    -h|--help)
      sed -n '2,10p' "$0"
      exit 0
      ;;
    *) echo "Unknown arg: $1" >&2; exit 1 ;;
  esac
done

if [[ ! -f "$ENGINE" ]]; then
  echo "Engine not found: $ENGINE" >&2
  echo "Build first: cmake --build build --target chess-uci" >&2
  exit 1
fi

mkdir -p data nets
LOG="data/nnue_overnight_$(date +%Y%m%d_%H%M%S).log"
echo "Logging to $LOG"
exec > >(tee -a "$LOG") 2>&1

GEN_HOURS=$(python3 -c "print(max(0.5, float('$HOURS') * 0.7))")
TRAIN_HOURS=$(python3 -c "print(max(0.25, float('$HOURS') * 0.3))")

echo "=== NNUE overnight ==="
echo "total≈${HOURS}h  gen≈${GEN_HOURS}h  train≈${TRAIN_HOURS}h"
echo "positions target=$POSITIONS  depth=$DEPTH  train_epochs=$TRAIN_EPOCHS"
echo "Ctrl+C anytime keeps flushed data + latest checkpoint/net."
echo

echo "--- Phase 1: generate data ---"
# Soft time budget via gtimeout/python wrapper; Ctrl+C still works on the child.
python3 - <<PY
import subprocess, signal, sys, time, os

cmd = [
    sys.executable, "scripts/nnue/generate_data.py",
    "--engine", "$ENGINE",
    "--out", "$DATA",
    "--positions", "$POSITIONS",
    "--depth", "$DEPTH",
    "--append",
]
budget = float("$GEN_HOURS") * 3600
proc = subprocess.Popen(cmd)
start = time.monotonic()

def forward(sig, _frame):
    proc.send_signal(sig)

signal.signal(signal.SIGINT, forward)
signal.signal(signal.SIGTERM, forward)

while proc.poll() is None:
    if time.monotonic() - start >= budget:
        print(f"\\ngenerate time budget ({float('$GEN_HOURS'):.1f}h) reached — stopping labeler", flush=True)
        proc.send_signal(signal.SIGINT)
        break
    time.sleep(1.0)

rc = proc.wait()
# 130 = interrupted; treat as success if we have data
sys.exit(0 if rc in (0, 130, -2, -15) else rc)
PY

ROWS=$(python3 -c "from pathlib import Path; p=Path('$DATA'); print(max(0, sum(1 for _ in p.open())-1) if p.is_file() else 0)")
echo "Data rows available: $ROWS"
if [[ "$ROWS" -lt 1000 ]]; then
  echo "Not enough data to train (<1000). Exiting." >&2
  exit 1
fi

echo
echo "--- Phase 2: train ---"
python3 scripts/nnue/train.py \
  --data "$DATA" \
  --epochs "$TRAIN_EPOCHS" \
  --max-hours "$TRAIN_HOURS" \
  --resume \
  --checkpoint "$CKPT" \
  --out "$OUT" \
  --export-every 5

echo
echo "Done. Net: $OUT"
echo "Restart chess-uci, or: setoption name EvalFile value $OUT"
echo "Log: $LOG"
