#!/usr/bin/env python3
"""Estimate ChessAI Elo by playing Stockfish at calibrated UCI_Elo anchors.

Stockfish UCI_Elo is anchored to CCRL 40/4 (approx. 1320–3190), not FIDE/Chess.com.
Results are approximate; run more games for a tighter interval.
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

try:
    import chess
    import chess.engine
except ImportError:
    print(
        "Missing dependency: python-chess\n"
        "  pip install -r scripts/requirements-elo.txt",
        file=sys.stderr,
    )
    sys.exit(1)

# Varied quiet openings (UCI move lists from startpos).
OPENINGS: list[list[str]] = [
    [],  # startpos
    ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5"],  # Ruy Lopez
    ["e2e4", "c7c5", "g1f3", "d7d6", "d2d4"],  # Sicilian
    ["d2d4", "d7d5", "c2c4", "e7e6", "b1c3"],  # QGD
    ["d2d4", "g8f6", "c2c4", "e7e6", "g1f3"],  # Nimzo/QID setup
    ["e2e4", "e7e5", "g1f3", "g8f6"],  # Petroff
    ["c2c4", "e7e5", "b1c3", "g8f6"],  # English
    ["e2e4", "c7c6", "d2d4", "d7d5"],  # Caro-Kann
    ["e2e4", "e7e6", "d2d4", "d7d5"],  # French
    ["g1f3", "d7d5", "g2g3", "g8f6", "f1g2"],  # Reti/KIA
    ["d2d4", "d7d5", "g1f3", "g8f6", "c1f4"],  # London
    ["e2e4", "g8f6", "e4e5", "f6d5"],  # Alekhine
]

DEFAULT_ANCHORS = [2800]
QUICK_ANCHORS = [3200]


@dataclass
class AnchorResult:
    elo: int
    wins: int = 0
    draws: int = 0
    losses: int = 0
    games: list[str] = field(default_factory=list)  # "1-0", "0-1", "1/2-1/2" from ours POV

    @property
    def n(self) -> int:
        return self.wins + self.draws + self.losses

    @property
    def score(self) -> float:
        """Score for ChessAI (1=win, 0.5=draw)."""
        if self.n == 0:
            return 0.5
        return (self.wins + 0.5 * self.draws) / self.n


def repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


def default_ours_path() -> Path:
    return repo_root() / "build" / "uci" / "chess-uci"


def resolve_stockfish(explicit: Optional[str]) -> Path:
    if explicit:
        p = Path(explicit).expanduser()
        if not p.is_file():
            raise FileNotFoundError(f"Stockfish binary not found: {p}")
        return p.resolve()

    env = os.environ.get("STOCKFISH")
    if env:
        p = Path(env).expanduser()
        if p.is_file():
            return p.resolve()

    for name in ("stockfish", "stockfish-17", "stockfish-16"):
        found = shutil.which(name)
        if found:
            return Path(found).resolve()

    raise FileNotFoundError(
        "Could not find Stockfish. Pass --stockfish, set $STOCKFISH, or install:\n"
        "  brew install stockfish"
    )


def resolve_ours(explicit: Optional[str]) -> Path:
    candidates: list[Path] = []
    if explicit:
        candidates.append(Path(explicit).expanduser())
    env = os.environ.get("CHESSAI_UCI")
    if env:
        candidates.append(Path(env).expanduser())
    candidates.append(default_ours_path())
    which = shutil.which("chess-uci")
    if which:
        candidates.append(Path(which))

    for p in candidates:
        if p.is_file():
            return p.resolve()

    raise FileNotFoundError(
        f"ChessAI UCI binary not found (tried {[str(c) for c in candidates]}).\n"
        "Build it first:\n"
        "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release\n"
        "  cmake --build build --target chess-uci"
    )


def parse_anchors(s: str) -> list[int]:
    vals = []
    for part in s.split(","):
        part = part.strip()
        if not part:
            continue
        v = int(part)
        # Stockfish clamp; keep user values but warn later if out of range.
        vals.append(v)
    if not vals:
        raise ValueError("No anchors given")
    return vals


def parse_tc(s: str) -> tuple[float, float]:
    """Parse 'base+inc' seconds, e.g. 10+0.1 -> (10.0, 0.1)."""
    if "+" not in s:
        return float(s), 0.0
    base, inc = s.split("+", 1)
    return float(base), float(inc)


def configure_ours(engine: chess.engine.SimpleEngine, hash_mb: int) -> None:
    opts: dict = {
        "Threads": 8,
        "Hash": hash_mb,
        "OwnBook": False,
        "Skill Level": 20,
    }
    # Classical by default while the net is still weak; flip UseNNUE when ready.
    eval_file = repo_root() / "nets" / "default.nnue"
    opts["UseNNUE"] = False
    if eval_file.is_file():
        opts["EvalFile"] = str(eval_file)
    engine.configure(opts)


def configure_stockfish(engine: chess.engine.SimpleEngine, hash_mb: int, elo: int) -> None:
    # Clamp to Stockfish's documented range when possible.
    elo_clamped = max(1320, min(3190, elo))
    opts: dict = {
        "Threads": 1,
        "Hash": hash_mb,
        "UCI_LimitStrength": True,
        "UCI_Elo": elo_clamped,
    }
    # Some builds expose Skill Level; LimitStrength overrides it.
    engine.configure(opts)


def make_movetime_limit(movetime_ms: int) -> chess.engine.Limit:
    return chess.engine.Limit(time=movetime_ms / 1000.0)


def play_game(
    ours: chess.engine.SimpleEngine,
    sf: chess.engine.SimpleEngine,
    opening: list[str],
    ours_is_white: bool,
    movetime_ms: Optional[int],
    tc: Optional[tuple[float, float]],
) -> str:
    """Return result from ChessAI's perspective: '1-0', '0-1', or '1/2-1/2'."""
    board = chess.Board()
    for uci in opening:
        board.push_uci(uci)

    if tc is not None:
        base, inc = tc
        clocks = {"white": base, "black": base}
        inc_s = inc
    else:
        clocks = None
        inc_s = 0.0
        assert movetime_ms is not None
        fixed_limit = make_movetime_limit(movetime_ms)

    while not board.is_game_over(claim_draw=True):
        white_to_move = board.turn == chess.WHITE
        use_ours = (white_to_move and ours_is_white) or (not white_to_move and not ours_is_white)
        engine = ours if use_ours else sf

        if clocks is not None:
            move_limit = chess.engine.Limit(
                white_clock=clocks["white"],
                black_clock=clocks["black"],
                white_inc=inc_s,
                black_inc=inc_s,
            )
        else:
            move_limit = fixed_limit

        t0 = time.perf_counter()
        result = engine.play(board, move_limit)
        elapsed = time.perf_counter() - t0

        if result.move is None:
            break
        board.push(result.move)

        if clocks is not None:
            side = "white" if white_to_move else "black"
            clocks[side] = max(0.0, clocks[side] - elapsed) + inc_s

    outcome = board.outcome(claim_draw=True)
    if outcome is None or outcome.winner is None:
        return "1/2-1/2"
    if outcome.winner == chess.WHITE:
        return "1-0" if ours_is_white else "0-1"
    return "0-1" if ours_is_white else "1-0"


def record(anchor: AnchorResult, ours_result: str) -> None:
    if ours_result == "1-0":
        anchor.wins += 1
    elif ours_result == "0-1":
        anchor.losses += 1
    else:
        anchor.draws += 1
    anchor.games.append(ours_result)


def elo_from_score(score: float, n: int) -> tuple[float, float]:
    """Return (delta_elo, se_delta) from score vs equal opponent.

    Extreme scores are smoothed so logodds stay finite.
    """
    if n <= 0:
        return 0.0, 400.0
    # Laplace-style clamp away from 0/1.
    s = min(max(score, 0.5 / n), 1.0 - 0.5 / n)
    delta = 400.0 * math.log10(s / (1.0 - s))
    # Binomial SE on score, delta method for Elo.
    var_s = s * (1.0 - s) / n
    # d(delta)/ds = 400 / (ln(10) * s * (1-s))
    d_ds = 400.0 / (math.log(10.0) * s * (1.0 - s))
    se = d_ds * math.sqrt(var_s)
    return delta, se


def combine_estimates(anchors: list[AnchorResult]) -> tuple[float, float, float]:
    """Inverse-variance weighted Elo estimate. Returns (mean, lo95, hi95)."""
    numer = 0.0
    denom = 0.0
    for a in anchors:
        if a.n == 0:
            continue
        delta, se = elo_from_score(a.score, a.n)
        est = a.elo + delta
        w = 1.0 / max(se * se, 1.0)
        numer += w * est
        denom += w
    if denom <= 0:
        return 2600.0, 2200.0, 3000.0
    mean = numer / denom
    se = math.sqrt(1.0 / denom)
    z = 1.96
    return mean, mean - z * se, mean + z * se


def fmt_score(a: AnchorResult) -> str:
    return f"{a.wins}-{a.draws}-{a.losses} ({100.0 * a.score:.1f}%)"


def run(args: argparse.Namespace) -> int:
    try:
        ours_path = resolve_ours(args.ours)
        sf_path = resolve_stockfish(args.stockfish)
    except FileNotFoundError as e:
        print(str(e), file=sys.stderr)
        return 1

    if args.quick:
        anchors_list = QUICK_ANCHORS
        total_games = 4 if args.games == 80 else args.games
    else:
        anchors_list = parse_anchors(args.anchors)
        total_games = args.games

    n_anchors = len(anchors_list)
    base = total_games // n_anchors
    rem = total_games % n_anchors
    games_per = [base + (1 if i < rem else 0) for i in range(n_anchors)]

    tc: Optional[tuple[float, float]] = None
    movetime_ms: Optional[int] = args.movetime
    if args.tc:
        tc = parse_tc(args.tc)
        movetime_ms = None

    print(f"Ours:      {ours_path}")
    print(f"Stockfish: {sf_path}")
    print(f"Anchors:   {anchors_list}")
    print(f"Games:     {total_games} ({games_per} per anchor)")
    if tc:
        print(f"TC:        {tc[0]}+{tc[1]}")
    else:
        print(f"Movetime:  {movetime_ms} ms")
    print(f"Hash:      {args.hash} MB, Threads: 1, OwnBook: false")
    print("Note: estimates are CCRL-anchored (Stockfish UCI_Elo), not Chess.com/FIDE.\n")

    anchors = [AnchorResult(elo=e) for e in anchors_list]
    game_idx = 0

    with chess.engine.SimpleEngine.popen_uci(str(ours_path)) as ours, chess.engine.SimpleEngine.popen_uci(
        str(sf_path)
    ) as sf:
        configure_ours(ours, args.hash)

        for anchor, n_games in zip(anchors, games_per):
            if n_games <= 0:
                continue
            configure_stockfish(sf, args.hash, anchor.elo)
            ours.ping()
            sf.ping()

            for g in range(n_games):
                opening = OPENINGS[game_idx % len(OPENINGS)]
                ours_white = g % 2 == 0
                try:
                    ours_result = play_game(ours, sf, opening, ours_white, movetime_ms, tc)
                except (chess.engine.EngineError, chess.engine.EngineTerminatedError) as ex:
                    print(f"\nEngine error: {ex}", file=sys.stderr)
                    return 1

                record(anchor, ours_result)
                game_idx += 1
                color = "W" if ours_white else "B"
                print(
                    f"[{game_idx}/{total_games}] SF@{anchor.elo} ours={color} "
                    f"result={ours_result}  anchor={fmt_score(anchor)}"
                )

    print("\n=== Per-anchor results (ChessAI score) ===")
    for a in anchors:
        if a.n == 0:
            continue
        delta, se = elo_from_score(a.score, a.n)
        est = a.elo + delta
        print(
            f"  vs SF {a.elo}: {fmt_score(a)}  "
            f"-> implied Elo ~ {est:.0f} (±{1.96 * se:.0f})"
        )

    mean, lo, hi = combine_estimates(anchors)
    total_w = sum(a.wins for a in anchors)
    total_d = sum(a.draws for a in anchors)
    total_l = sum(a.losses for a in anchors)
    total_n = total_w + total_d + total_l
    overall = (total_w + 0.5 * total_d) / total_n if total_n else 0.5

    print("\n=== Summary ===")
    print(f"Games:  {total_n}  W-D-L {total_w}-{total_d}-{total_l}  score {100.0 * overall:.1f}%")
    print(f"Estimated Elo: {mean:.0f}  (95% CI ~ {lo:.0f} – {hi:.0f})")
    print(
        "Caveat: approximate, time-control dependent, CCRL-anchored via Stockfish "
        "UCI_LimitStrength. Not comparable to Chess.com bot ratings."
    )
    return 0


def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="Estimate ChessAI Elo vs Stockfish UCI_Elo anchors")
    p.add_argument(
        "--ours",
        default=None,
        help=f"Path to chess-uci (default: {default_ours_path()})",
    )
    p.add_argument(
        "--stockfish",
        default=None,
        help="Path to stockfish (default: PATH or $STOCKFISH)",
    )
    p.add_argument("--games", type=int, default=80, help="Total games across all anchors (default 80)")
    p.add_argument(
        "--movetime",
        type=int,
        default=1000,
        help="Milliseconds per move (default 1000). Ignored if --tc is set.",
    )
    p.add_argument(
        "--tc",
        default=None,
        help="Clock time control base+inc in seconds, e.g. 10+0.1 (overrides --movetime)",
    )
    p.add_argument(
        "--anchors",
        default=",".join(str(x) for x in DEFAULT_ANCHORS),
        help="Comma-separated Stockfish UCI_Elo anchors",
    )
    p.add_argument("--hash", type=int, default=64, help="Hash size MB for both engines (default 64)")
    p.add_argument(
        "--quick",
        action="store_true",
        help="Faster estimate: 4 games, anchors 2400/2600/2800",
    )
    return p


def main() -> None:
    args = build_argparser().parse_args()
    sys.exit(run(args))


if __name__ == "__main__":
    main()
