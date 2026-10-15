#!/usr/bin/env python3
"""Export a saved checkpoint (.pt) to nets/default.nnue."""

from __future__ import annotations

import argparse
from pathlib import Path

import torch

from train import NnueNet, export_nnue


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--checkpoint", default="data/nnue_checkpoint.pt")
    p.add_argument("--out", default="nets/default.nnue")
    args = p.parse_args()

    model = NnueNet()
    state = torch.load(args.checkpoint, map_location="cpu")
    model.load_state_dict(state)
    export_nnue(model, Path(args.out))


if __name__ == "__main__":
    main()
