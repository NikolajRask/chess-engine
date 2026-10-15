#!/usr/bin/env python3
"""Generate (fen, score_cp) labels for NNUE training via UCI engine search.

Interruptible: Ctrl+C flushes already-labeled rows to --out (append-safe).
"""

from __future__ import annotations

import argparse
import random
import signal
import sys
from pathlib import Path

import chess
import chess.engine


START_FENS = [
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
    "rnbqkb1r/pppp1ppp/5n2/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3",
    "rnbqkb1r/ppp1pppp/5n2/3p4/2PP4/8/PP2PPPP/RNBQKBNR w KQkq - 0 3",
    "rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",
]


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--engine",
        default="build/uci/chess-uci",
        help="UCI engine path used for labels (classical eval recommended)",
    )
    p.add_argument("--out", default="data/nnue_positions_d7.csv", help="Output CSV path")
    p.add_argument("--positions", type=int, default=1_000_000, help="Target labeled positions")
    p.add_argument("--depth", type=int, default=7, help="Search depth for labels")
    p.add_argument("--movetime", type=int, default=0, help="If >0, use movetime ms instead of depth")
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--use-nnue", action="store_true", help="Allow NNUE while labeling (default off)")
    p.add_argument(
        "--append",
        action="store_true",
        help="Append to existing CSV instead of overwriting",
    )
    return p.parse_args()


def score_from_info(info: chess.engine.InfoDict, board: chess.Board) -> int | None:
    score = info.get("score")
    if score is None:
        return None
    pov = score.pov(board.turn)
    if pov.is_mate():
        mate = pov.mate()
        if mate is None:
            return None
        return 30000 - abs(mate) * 10 if mate > 0 else -30000 + abs(mate) * 10
    cp = pov.score(mate_score=30000)
    return int(cp) if cp is not None else None


def count_existing(path: Path) -> int:
    if not path.is_file():
        return 0
    with path.open(encoding="utf-8") as f:
        # header + rows
        n = sum(1 for _ in f) - 1
    return max(0, n)


def main() -> None:
    args = parse_args()
    random.seed(args.seed)
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)

    existing = count_existing(out) if args.append else 0
    if args.append and existing > 0:
        print(f"resuming append: {existing} rows already in {out}", flush=True)
        mode = "a"
        need_header = False
    else:
        mode = "w"
        need_header = True
        existing = 0

    limit = (
        chess.engine.Limit(time=args.movetime / 1000.0)
        if args.movetime > 0
        else chess.engine.Limit(depth=args.depth)
    )

    stop = {"flag": False}

    def _handle(_sig, _frame) -> None:
        print("\nSIGINT — flushing and exiting…", flush=True)
        stop["flag"] = True

    signal.signal(signal.SIGINT, _handle)
    signal.signal(signal.SIGTERM, _handle)

    written = existing
    target = args.positions
    # When appending, --positions is total target including existing.
    remaining_goal = max(0, target - existing) if args.append else target

    with out.open(mode, encoding="utf-8") as f, chess.engine.SimpleEngine.popen_uci(
        args.engine
    ) as eng:
        if need_header:
            f.write("fen,score\n")
            f.flush()
        eng.configure({"UseNNUE": args.use_nnue, "OwnBook": False, "Hash": 64})

        batch = 0
        while (written - existing) < remaining_goal and not stop["flag"]:
            fen = random.choice(START_FENS)
            board = chess.Board(fen)
            plies = random.randint(4, 40)
            for _ in range(plies):
                if board.is_game_over():
                    break
                moves = list(board.legal_moves)
                if not moves:
                    break
                weights = []
                for m in moves:
                    w = 3 if board.is_capture(m) else 1
                    if board.gives_check(m):
                        w += 2
                    weights.append(w)
                board.push(random.choices(moves, weights=weights, k=1)[0])

            if board.is_game_over():
                continue

            for _ in range(random.randint(1, 3)):
                if (
                    board.is_game_over()
                    or stop["flag"]
                    or (written - existing) >= remaining_goal
                ):
                    break
                info = eng.analyse(board, limit)
                sc = score_from_info(info, board)
                if sc is None:
                    break
                sc = max(-2000, min(2000, sc))
                f.write(f"{board.fen()},{sc}\n")
                written += 1
                batch += 1
                moves = list(board.legal_moves)
                if not moves:
                    break
                board.push(random.choice(moves))

            if batch >= 100:
                f.flush()
                print(f"labeled {written}/{target}", flush=True)
                batch = 0

        f.flush()

    print(f"wrote total {written} positions → {out}")
    if stop["flag"]:
        sys.exit(130)


if __name__ == "__main__":
    main()
