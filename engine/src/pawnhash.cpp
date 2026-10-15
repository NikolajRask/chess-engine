#include "chess/pawnhash.hpp"

#include "chess/bitboard.hpp"
#include "chess/position.hpp"
#include "chess/zobrist.hpp"

namespace chess {

PawnHash::PawnHash() { clear(); }

void PawnHash::clear() {
  for (auto& e : table_) e = PawnEntry{};
  hits_ = 0;
  misses_ = 0;
}

bool PawnHash::probe(Bitboard key, int& mg, int& eg) const {
  const PawnEntry& e = table_[key & (kSize - 1)];
  if (e.key == key) {
    ++hits_;
    mg = e.mg;
    eg = e.eg;
    return true;
  }
  ++misses_;
  return false;
}

void PawnHash::store(Bitboard key, int mg, int eg) {
  PawnEntry& e = table_[key & (kSize - 1)];
  e.key = key;
  e.mg = static_cast<int16_t>(mg);
  e.eg = static_cast<int16_t>(eg);
}

PawnHash& threadPawnHash() {
  thread_local PawnHash ph;
  return ph;
}

Bitboard pawnKey(const Position& pos) {
  Bitboard h = 0;
  Bitboard wp = pos.pieces(Piece::WPawn);
  while (wp) {
    const Square sq = bb::lsbSquare(wp);
    wp &= wp - 1;
    h ^= zobrist::piece[static_cast<int>(sq)][static_cast<int>(Piece::WPawn) - 1];
  }
  Bitboard bp = pos.pieces(Piece::BPawn);
  while (bp) {
    const Square sq = bb::lsbSquare(bp);
    bp &= bp - 1;
    h ^= zobrist::piece[static_cast<int>(sq)][static_cast<int>(Piece::BPawn) - 1];
  }
  return h;
}

}  // namespace chess
