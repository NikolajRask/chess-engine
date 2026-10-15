#pragma once

#include "chess/types.hpp"

#include <cstdint>

namespace chess {

class Position;

struct PawnEntry {
  Bitboard key = 0;
  int16_t mg = 0;
  int16_t eg = 0;
};

// Thread-local pawn structure cache (White − Black mid/end).
class PawnHash {
 public:
  static constexpr size_t kSize = 1 << 18;  // 262144 entries

  PawnHash();
  bool probe(Bitboard key, int& mg, int& eg) const;
  void store(Bitboard key, int mg, int eg);
  void clear();
  std::uint64_t hits() const { return hits_; }
  std::uint64_t misses() const { return misses_; }

 private:
  PawnEntry table_[kSize]{};
  mutable std::uint64_t hits_ = 0;
  mutable std::uint64_t misses_ = 0;
};

PawnHash& threadPawnHash();

// Zobrist key over white and black pawns only.
Bitboard pawnKey(const Position& pos);

}  // namespace chess
