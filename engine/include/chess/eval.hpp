#pragma once

#include "chess/position.hpp"
#include "chess/types.hpp"

namespace chess {

// Apply material + PST + phase for placing (sign=+1) or removing (sign=-1) a piece.
void applyMaterialDelta(Square sq, Piece p, int sign, int& mg, int& eg, int& phase);

// Recompute material/PST/phase from the board (used by Position::refreshEval).
void computeMaterial(const Position& pos, int& mg, int& eg, int& phase);

int evaluate(const Position& pos);

// Full recompute of evaluate() without using the incremental cache (for tests/asserts).
int evaluateFull(const Position& pos);

}  // namespace chess
