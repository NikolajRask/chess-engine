#pragma once

#include "chess/types.hpp"

namespace chess {
namespace magic {

void init();

Bitboard bishopAttacks(Square sq, Bitboard occ);
Bitboard rookAttacks(Square sq, Bitboard occ);

inline Bitboard queenAttacks(Square sq, Bitboard occ) {
  return bishopAttacks(sq, occ) | rookAttacks(sq, occ);
}

}  // namespace magic
}  // namespace chess
