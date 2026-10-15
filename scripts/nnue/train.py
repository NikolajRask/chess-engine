#!/usr/bin/env python3
"""Train a small perspective NNUE on MacBook (MPS/CPU) and export nets/default.nnue.

Interruptible: Ctrl+C saves checkpoint + exports the latest net.
Resume with --resume. Optional --max-hours stops cleanly after a time budget.
"""

from __future__ import annotations

import argparse
import csv
import signal
import struct
import time
from pathlib import Path

import chess
import numpy as np
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, Dataset

from features import ACC_DIM, HIDDEN_DIM, INPUT_DIM, QA, QB, encode_board

MAGIC = 0x49415343
VERSION = 1
# FT ReLU is soft-scaled into [0,1] before L1; export multiplies by QA/FT_SCALE.
FT_SCALE = 4.0


def pick_device() -> torch.device:
    if torch.backends.mps.is_available():
        return torch.device("mps")
    return torch.device("cpu")


class NnueNet(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.ft = nn.Linear(INPUT_DIM, ACC_DIM, bias=True)
        self.l1 = nn.Linear(2 * ACC_DIM, HIDDEN_DIM, bias=True)
        self.l2 = nn.Linear(HIDDEN_DIM, 1, bias=True)
        nn.init.uniform_(self.ft.weight, -1.0 / 8.0, 1.0 / 8.0)
        nn.init.zeros_(self.ft.bias)
        nn.init.uniform_(self.l1.weight, -0.5, 0.5)
        nn.init.zeros_(self.l1.bias)
        nn.init.uniform_(self.l2.weight, -5.0, 5.0)
        nn.init.zeros_(self.l2.bias)

    def forward(self, stm: torch.Tensor, nstm: torch.Tensor) -> torch.Tensor:
        acc_stm = torch.clamp(torch.relu(self.ft(stm)) / FT_SCALE, 0.0, 1.0)
        acc_nstm = torch.clamp(torch.relu(self.ft(nstm)) / FT_SCALE, 0.0, 1.0)
        x = torch.cat([acc_stm, acc_nstm], dim=1)
        h = torch.relu(self.l1(x))
        return self.l2(h).squeeze(1)


class FenDataset(Dataset):
    def __init__(self, path: Path, limit: int | None = None) -> None:
        self.rows: list[tuple[str, float]] = []
        with path.open(newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for i, row in enumerate(reader):
                if limit is not None and i >= limit:
                    break
                self.rows.append((row["fen"], float(row["score"])))

    def __len__(self) -> int:
        return len(self.rows)

    def __getitem__(self, idx: int) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        fen, score = self.rows[idx]
        board = chess.Board(fen)
        w, b = encode_board(board)
        if board.turn == chess.WHITE:
            stm, nstm = w, b
        else:
            stm, nstm = b, w
        return (
            torch.from_numpy(stm),
            torch.from_numpy(nstm),
            torch.tensor(score, dtype=torch.float32),
        )


def export_nnue(model: NnueNet, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    ft_w = model.ft.weight.detach().cpu().numpy()  # [ACC, INPUT]
    ft_b = model.ft.bias.detach().cpu().numpy()
    l1_w = model.l1.weight.detach().cpu().numpy()
    l1_b = model.l1.bias.detach().cpu().numpy()
    l2_w = model.l2.weight.detach().cpu().numpy().reshape(-1)
    l2_b = float(model.l2.bias.detach().cpu().numpy().reshape(-1)[0])

    scale = QA / FT_SCALE
    ft_w_i16 = np.zeros((INPUT_DIM, ACC_DIM), dtype=np.int16)
    for feat in range(INPUT_DIM):
        col = np.clip(np.rint(ft_w[:, feat] * scale), -32768, 32767)
        ft_w_i16[feat, :] = col.astype(np.int16)
    ft_b_i16 = np.clip(np.rint(ft_b * scale), -32768, 32767).astype(np.int16)

    l1_w_i16 = np.clip(np.rint(l1_w), -32768, 32767).astype(np.int16)
    l1_b_i16 = np.clip(np.rint(l1_b * QA), -32768, 32767).astype(np.int16)
    l2_w_i16 = np.clip(np.rint(l2_w * QB), -32768, 32767).astype(np.int16)
    l2_b_i16 = np.int16(np.clip(np.rint(l2_b * QB * QA), -32768, 32767))

    with path.open("wb") as f:
        f.write(struct.pack("<IIII", MAGIC, VERSION, ACC_DIM, HIDDEN_DIM))
        f.write(ft_w_i16.reshape(-1).tobytes())
        f.write(ft_b_i16.tobytes())
        f.write(l1_w_i16.reshape(-1).tobytes())
        f.write(l1_b_i16.tobytes())
        f.write(l2_w_i16.tobytes())
        f.write(struct.pack("<h", int(l2_b_i16)))
    print(f"exported {path}", flush=True)


def save_checkpoint(
    path: Path,
    model: NnueNet,
    opt: torch.optim.Optimizer,
    epoch: int,
    best_mse: float,
) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "model": model.state_dict(),
            "opt": opt.state_dict(),
            "epoch": epoch,
            "best_mse": best_mse,
        },
        path,
    )


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--data", default="data/nnue_positions.csv")
    p.add_argument("--epochs", type=int, default=100, help="Max epochs (default 100)")
    p.add_argument("--batch", type=int, default=256)
    p.add_argument("--lr", type=float, default=3e-3)
    p.add_argument("--limit", type=int, default=0, help="Optional row cap (0=all)")
    p.add_argument("--out", default="nets/default.nnue")
    p.add_argument("--checkpoint", default="data/nnue_checkpoint.pt")
    p.add_argument("--resume", action="store_true", help="Resume from --checkpoint if present")
    p.add_argument(
        "--max-hours",
        type=float,
        default=0.0,
        help="Stop cleanly after this many hours (0 = no time limit)",
    )
    p.add_argument(
        "--export-every",
        type=int,
        default=5,
        help="Export .nnue every N epochs (also on Ctrl+C / finish)",
    )
    return p.parse_args()


def main() -> None:
    args = parse_args()
    device = pick_device()
    print(f"device={device}", flush=True)
    limit = args.limit if args.limit > 0 else None
    ds = FenDataset(Path(args.data), limit=limit)
    if len(ds) == 0:
        raise SystemExit(f"no data in {args.data}")
    print(f"dataset={len(ds)} positions", flush=True)
    loader = DataLoader(ds, batch_size=args.batch, shuffle=True, num_workers=0)

    model = NnueNet().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    loss_fn = nn.MSELoss()
    start_epoch = 0
    best_mse = float("inf")

    ckpt_path = Path(args.checkpoint)
    if args.resume and ckpt_path.is_file():
        blob = torch.load(ckpt_path, map_location=device)
        if isinstance(blob, dict) and "model" in blob:
            model.load_state_dict(blob["model"])
            if "opt" in blob:
                opt.load_state_dict(blob["opt"])
            start_epoch = int(blob.get("epoch", 0))
            best_mse = float(blob.get("best_mse", best_mse))
            print(f"resumed epoch={start_epoch} best_mse={best_mse:.2f}", flush=True)
        else:
            # Legacy: bare state_dict
            model.load_state_dict(blob)
            print("resumed legacy checkpoint (weights only)", flush=True)

    stop = {"flag": False}

    def _handle(_sig, _frame) -> None:
        print("\nSIGINT — will save after this epoch…", flush=True)
        stop["flag"] = True

    signal.signal(signal.SIGINT, _handle)
    signal.signal(signal.SIGTERM, _handle)

    t0 = time.monotonic()
    max_seconds = args.max_hours * 3600.0 if args.max_hours > 0 else None

    for epoch in range(start_epoch, args.epochs):
        if stop["flag"]:
            break
        if max_seconds is not None and (time.monotonic() - t0) >= max_seconds:
            print(f"max-hours ({args.max_hours}) reached — stopping", flush=True)
            break

        total = 0.0
        n = 0
        model.train()
        for stm, nstm, y in loader:
            if stop["flag"]:
                break
            stm = stm.to(device)
            nstm = nstm.to(device)
            y = y.to(device)
            pred = model(stm, nstm)
            loss = loss_fn(pred, y)
            opt.zero_grad()
            loss.backward()
            opt.step()
            total += float(loss.item()) * len(y)
            n += len(y)

        mse = total / max(n, 1)
        print(f"epoch {epoch + 1}/{args.epochs}  mse={mse:.2f}", flush=True)

        if mse < best_mse:
            best_mse = mse

        save_checkpoint(ckpt_path, model, opt, epoch + 1, best_mse)

        if (epoch + 1) % max(1, args.export_every) == 0 or stop["flag"]:
            export_nnue(model.cpu(), Path(args.out))
            model.to(device)

    # Final export
    export_nnue(model.cpu(), Path(args.out))
    save_checkpoint(ckpt_path, model, opt, min(args.epochs, start_epoch + 1), best_mse)

    model.eval()
    ys: list[torch.Tensor] = []
    ps: list[torch.Tensor] = []
    with torch.no_grad():
        for i, (stm, nstm, y) in enumerate(DataLoader(ds, batch_size=512)):
            if i >= 8:
                break
            ps.append(model(stm, nstm))
            ys.append(y)
    y = torch.cat(ys)
    p = torch.cat(ps)
    corr = float(torch.corrcoef(torch.stack([y, p]))[0, 1])
    mae = float((y - p).abs().mean())
    elapsed_h = (time.monotonic() - t0) / 3600.0
    print(f"train sample corr={corr:.3f} mae={mae:.1f}cp  elapsed={elapsed_h:.2f}h", flush=True)


if __name__ == "__main__":
    main()
