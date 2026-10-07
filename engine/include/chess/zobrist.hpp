#pragma once

#include "chess/types.hpp"

namespace chess {

class Position;

namespace zobrist {

extern Bitboard piece[64][12];
extern Bitboard side;
extern Bitboard castling[16];
extern Bitboard epFile[8];

void init();
Bitboard hashPosition(const Position& pos);

}  // namespace zobrist
}  // namespace chess
