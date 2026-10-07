#pragma once

#include "chess/types.hpp"

namespace chess {

namespace bb {

extern Bitboard lineBB[64][64];
extern Bitboard betweenBB[64][64];
extern Bitboard rayBB[64][8];
extern Bitboard pawnAttacks[2][64];
extern Bitboard pawnAttackers[2][64];
extern Bitboard knightAttacks[64];
extern Bitboard kingAttacks[64];

void init();

Bitboard slidingAttacks(Square sq, Bitboard occ, bool bishop);
Bitboard attackersTo(Square sq, Color byColor, const Bitboard pieces[13],
                     Bitboard occ);

inline Bitboard northOne(Bitboard b) { return b << 8; }
inline Bitboard southOne(Bitboard b) { return b >> 8; }
inline Bitboard eastOne(Bitboard b) { return (b & 0xfefefefefefefefeULL) << 1; }
inline Bitboard westOne(Bitboard b) { return (b & 0x7f7f7f7f7f7f7f7fULL) >> 1; }

inline Bitboard poplsb(Bitboard& b) {
  const Bitboard lsb = b & -b;
  b ^= lsb;
  return lsb;
}

inline Square lsbSquare(Bitboard b) {
  return static_cast<Square>(__builtin_ctzll(b));
}

inline int popcount(Bitboard b) { return __builtin_popcountll(b); }

inline bool moreThanOne(Bitboard b) { return b & (b - 1); }

}  // namespace bb

}  // namespace chess
