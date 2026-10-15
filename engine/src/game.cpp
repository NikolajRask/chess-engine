#include "chess/game.hpp"

#include "chess/movegen.hpp"

#include <algorithm>

namespace chess {

std::string_view outcomeToString(GameOutcome o) {
  switch (o) {
    case GameOutcome::Ongoing:
      return "ongoing";
    case GameOutcome::Checkmate:
      return "checkmate";
    case GameOutcome::Stalemate:
      return "stalemate";
    case GameOutcome::DrawFiftyMove:
      return "fifty_move";
    case GameOutcome::DrawSeventyFiveMove:
      return "seventy_five_move";
    case GameOutcome::DrawInsufficientMaterial:
      return "insufficient_material";
    case GameOutcome::DrawThreefold:
      return "threefold";
    case GameOutcome::DrawFivefold:
      return "fivefold";
  }
  return "ongoing";
}

int repetitionCount(Bitboard key, const std::vector<Bitboard>& history, int halfmove) {
  if (history.empty()) return 0;
  const int n = static_cast<int>(history.size());
  const int oldest = std::max(0, n - 1 - std::max(0, halfmove));
  int count = 0;
  // Same side to move: every other ply.
  for (int i = n - 1; i >= oldest; i -= 2) {
    if (history[static_cast<size_t>(i)] == key) ++count;
  }
  return count;
}

bool isThreefoldRepetition(Bitboard key, const std::vector<Bitboard>& history, int halfmove) {
  return repetitionCount(key, history, halfmove) >= 3;
}

bool isFivefoldRepetition(Bitboard key, const std::vector<Bitboard>& history, int halfmove) {
  return repetitionCount(key, history, halfmove) >= 5;
}

GameOutcome gameOutcome(Position& pos, const std::vector<Bitboard>& history) {
  // Automatic rules first (FIDE 9.6), then claimable / material / mate.
  if (pos.isSeventyFiveMoveDraw()) return GameOutcome::DrawSeventyFiveMove;
  if (isFivefoldRepetition(pos.hash(), history, pos.halfmoveClock())) {
    return GameOutcome::DrawFivefold;
  }
  if (pos.isInsufficientMaterial()) return GameOutcome::DrawInsufficientMaterial;
  if (pos.isFiftyMoveDraw()) return GameOutcome::DrawFiftyMove;
  if (isThreefoldRepetition(pos.hash(), history, pos.halfmoveClock())) {
    return GameOutcome::DrawThreefold;
  }

  MoveList moves;
  MoveGen::generateLegal(pos, moves);
  if (moves.empty()) {
    return pos.inCheck(pos.sideToMove()) ? GameOutcome::Checkmate : GameOutcome::Stalemate;
  }
  return GameOutcome::Ongoing;
}

std::vector<Bitboard> applyUciMoves(Position& pos, const std::vector<std::string>& moves) {
  std::vector<Bitboard> history;
  history.reserve(moves.size() + 1);
  history.push_back(pos.hash());
  for (const std::string& u : moves) {
    auto m = legalMoveFromUci(pos, u);
    if (!m) break;
    Undo undo;
    pos.makeMove(*m, undo);
    history.push_back(pos.hash());
  }
  return history;
}

}  // namespace chess
