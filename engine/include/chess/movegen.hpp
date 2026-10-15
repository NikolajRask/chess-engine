#pragma once

#include "chess/position.hpp"

#include <optional>
#include <string_view>

namespace chess {

struct MoveList {
  static constexpr int kCapacity = 256;
  Move moves[kCapacity]{};
  int size = 0;

  void clear() { size = 0; }
  bool empty() const { return size == 0; }
  int count() const { return size; }

  void push(const Move& m) {
    if (size < kCapacity) moves[size++] = m;
  }

  Move& operator[](int i) { return moves[i]; }
  const Move& operator[](int i) const { return moves[i]; }

  Move* begin() { return moves; }
  Move* end() { return moves + size; }
  const Move* begin() const { return moves; }
  const Move* end() const { return moves + size; }

  Move& front() { return moves[0]; }
  const Move& front() const { return moves[0]; }
};

class MoveGen {
 public:
  static void generateLegal(Position& pos, MoveList& moves);
  static void generateLegalCaptures(Position& pos, MoveList& moves);
  static void generatePseudoLegal(Position& pos, MoveList& moves);
};

int perft(Position& pos, int depth);

// Resolve a UCI string to a fully flagged legal move in `pos`.
std::optional<Move> legalMoveFromUci(Position& pos, std::string_view uci);

}  // namespace chess
