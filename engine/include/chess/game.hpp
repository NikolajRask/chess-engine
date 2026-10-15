#pragma once

#include "chess/position.hpp"
#include "chess/types.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace chess {

// FIDE draw / terminal outcomes detectable from the position + game hash history.
enum class GameOutcome : uint8_t {
  Ongoing = 0,
  Checkmate,             // side to move is mated
  Stalemate,             // no legal moves, not in check
  DrawFiftyMove,         // claimable (≥100 half-moves); treated as draw by engines
  DrawSeventyFiveMove,   // automatic (≥150 half-moves)
  DrawInsufficientMaterial,
  DrawThreefold,         // claimable (≥3 occurrences of the same position)
  DrawFivefold,          // automatic (≥5 occurrences)
};

inline bool isDrawOutcome(GameOutcome o) {
  return o == GameOutcome::Stalemate || o == GameOutcome::DrawFiftyMove ||
         o == GameOutcome::DrawSeventyFiveMove ||
         o == GameOutcome::DrawInsufficientMaterial ||
         o == GameOutcome::DrawThreefold || o == GameOutcome::DrawFivefold;
}

inline bool isTerminalOutcome(GameOutcome o) { return o != GameOutcome::Ongoing; }

std::string_view outcomeToString(GameOutcome o);

// Count how many times `key` appears in `history` within the last `halfmove` plies
// (inclusive of the current entry if it is already stored at the end of history).
int repetitionCount(Bitboard key, const std::vector<Bitboard>& history, int halfmove);

// `history` must include the current position hash as its last element.
bool isThreefoldRepetition(Bitboard key, const std::vector<Bitboard>& history, int halfmove);
bool isFivefoldRepetition(Bitboard key, const std::vector<Bitboard>& history, int halfmove);

// Full terminal-status check. `history` includes the current root hash as last element.
// Generates legal moves when needed for mate/stalemate (may temporarily make/unmake).
GameOutcome gameOutcome(Position& pos, const std::vector<Bitboard>& history);

// Apply UCI moves to `pos`, returning Zobrist hashes from the starting position
// through the final position (inclusive). Stops early on an illegal move.
std::vector<Bitboard> applyUciMoves(Position& pos, const std::vector<std::string>& moves);

}  // namespace chess
