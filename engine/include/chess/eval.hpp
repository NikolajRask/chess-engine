#pragma once

#include "chess/position.hpp"
#include "chess/types.hpp"

namespace chess {

// Apply material + PST + phase for placing (sign=+1) or removing (sign=-1) a piece.
void applyMaterialDelta(Square sq, Piece p, int sign, int& mg, int& eg, int& phase);

// Recompute material/PST/phase from the board (used by Position::refreshEval).
void computeMaterial(const Position& pos, int& mg, int& eg, int& phase);

// Side-to-move relative score. Uses NNUE when enabled and loaded; else classical.
int evaluate(const Position& pos);

// Classical eval only (material + PST + positional + pawn hash). Ignores NNUE.
int evaluateClassical(const Position& pos);

// Full classical recompute without incremental material cache (for tests).
int evaluateFull(const Position& pos);

// Drawish endgame scale in [0, 128]; 128 = full weight.
int endgameScaleFactor(const Position& pos);

// Disable pawn hash for tests (returns previous value).
bool setPawnHashEnabled(bool on);

}  // namespace chess
