#pragma once

#include "chess/position.hpp"
#include "chess/types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace chess {

struct SearchLimits {
  int maxDepth = 64;
  int timeMs = 2000;
};

struct SearchResult {
  std::string bestMoveUci;
  int scoreCp = 0;
  int depthReached = 0;
};

struct TTEntry {
  Bitboard key = 0;
  int16_t score = 0;
  int8_t depth = -1;
  uint8_t flag = 0;  // 0 exact, 1 upper (alpha), 2 lower (beta)
  Move bestMove{};
};

class Searcher {
 public:
  SearchResult search(Position& root, SearchLimits limits);

 private:
  static constexpr int kMaxDepth = 64;
  static constexpr size_t kTTSize = 1 << 22;  // 4M entries
  static constexpr int kMateScore = 30000;
  static constexpr int kInf = 32000;

  int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool allowNull);
  int quiescence(Position& pos, int alpha, int beta, int ply);
  void orderMoves(Position& pos, std::vector<Move>& moves, const Move& ttMove,
                  int ply);
  bool shouldStop() const;
  int scoreToTT(int score, int ply) const;
  int scoreFromTT(int score, int ply) const;
  void storeTT(Bitboard key, int score, int depth, int flag, const Move& best,
               int ply);
  bool probeTT(Bitboard key, int depth, int alpha, int beta, int ply, int& score,
               Move& ttMove);

  std::vector<TTEntry> tt_;
  Move killers_[128][2]{};
  int history_[64][64]{};

  std::chrono::steady_clock::time_point start_;
  std::atomic<bool> stop_{false};
  SearchLimits limits_{};
  Move pvMove_{};
  int completedDepth_ = 0;
  int score_ = 0;
  int nodes_ = 0;
};

SearchResult findBestMove(const std::string& fen, SearchLimits limits);

}  // namespace chess
