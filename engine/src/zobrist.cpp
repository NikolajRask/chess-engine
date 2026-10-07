#include "chess/zobrist.hpp"

#include "chess/position.hpp"

#include <random>

namespace chess {
namespace zobrist {

Bitboard piece[64][12];
Bitboard side;
Bitboard castling[16];
Bitboard epFile[8];

void init() {
  std::mt19937_64 rng(0xC0FFEE);
  for (int sq = 0; sq < 64; ++sq) {
    for (int p = 0; p < 12; ++p) {
      piece[sq][p] = rng();
    }
  }
  side = rng();
  for (int i = 0; i < 16; ++i) castling[i] = rng();
  for (int i = 0; i < 8; ++i) epFile[i] = rng();
}

Bitboard hashPosition(const Position& pos) {
  Bitboard h = 0;
  for (int sq = 0; sq < 64; ++sq) {
    const Piece p = pos.pieceOn(static_cast<Square>(sq));
    if (p != Piece::None) {
      h ^= piece[sq][static_cast<int>(p) - 1];
    }
  }
  if (pos.sideToMove() == Color::Black) h ^= side;
  h ^= castling[pos.castlingRights() & 15];
  const int ep = fileOf(pos.epSquare());
  if (pos.epSquare() != Square::None) h ^= epFile[ep];
  return h;
}

}  // namespace zobrist
}  // namespace chess
