#pragma once

#include "chess/position.hpp"

#include <vector>

namespace chess {

class MoveGen {
 public:
  static void generateLegal(Position& pos, std::vector<Move>& moves);
  static void generateLegalCaptures(Position& pos, std::vector<Move>& moves);
  static void generatePseudoLegal(Position& pos, std::vector<Move>& moves);
};

int perft(Position& pos, int depth);

}  // namespace chess
