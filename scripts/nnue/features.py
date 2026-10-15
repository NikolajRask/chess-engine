"""Perspective 768 piece-square features matching engine/src/nnue.cpp."""

from __future__ import annotations

import chess
import numpy as np

PIECE_TO_TYPE = {
    chess.PAWN: 0,
    chess.KNIGHT: 1,
    chess.BISHOP: 2,
    chess.ROOK: 3,
    chess.QUEEN: 4,
    chess.KING: 5,
}

INPUT_DIM = 768
ACC_DIM = 256
HIDDEN_DIM = 32
QA = 127
QB = 64  # L2 weight scale; output scale = QA * QB


def feature_index(perspective_white: bool, sq: int, piece: chess.Piece) -> int:
    type_idx = PIECE_TO_TYPE[piece.piece_type]
    is_white = piece.color == chess.WHITE
    if perspective_white:
        if not is_white:
            type_idx += 6
        s = sq
    else:
        if is_white:
            type_idx += 6
        s = sq ^ 63
    return type_idx * 64 + s


def encode_board(board: chess.Board) -> tuple[np.ndarray, np.ndarray]:
    """Return dense float32 feature vectors for white and black perspectives."""
    w = np.zeros(INPUT_DIM, dtype=np.float32)
    b = np.zeros(INPUT_DIM, dtype=np.float32)
    for sq, piece in board.piece_map().items():
        w[feature_index(True, sq, piece)] = 1.0
        b[feature_index(False, sq, piece)] = 1.0
    return w, b


def stm_features(board: chess.Board) -> tuple[np.ndarray, np.ndarray]:
    w, b = encode_board(board)
    if board.turn == chess.WHITE:
        return w, b
    return b, w
