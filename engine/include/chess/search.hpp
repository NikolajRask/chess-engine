#pragma once

#include "chess/position.hpp"
#include "chess/types.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace chess {

struct SearchLimits {
  int maxDepth = 64;
  int timeMs = 2000;  // legacy HTTP fixed budget
  int movetime = 0;
  int wtime = 0;
  int btime = 0;
  int winc = 0;
  int binc = 0;
  int movestogo = 0;
  std::uint64_t nodes = 0;  // 0 = unlimited
  int threads = 1;
  int skillLevel = 20;  // 0–20
  bool useBook = true;
  bool infinite = false;
  size_t hashMb = 16;  // TT size hint
};

struct SearchResult {
  std::string bestMoveUci;
  int scoreCp = 0;
  int depthReached = 0;
  bool fromBook = false;
};

struct TTEntry {
  Bitboard key = 0;
  int16_t score = 0;
  int8_t depth = -1;
  uint8_t flag = 0;  // 0 exact, 1 upper (alpha), 2 lower (beta)
  uint8_t generation = 0;
  Move bestMove{};
};

using SearchInfoFn = std::function<void(int depth, int scoreCp, std::uint64_t nodes)>;

struct SharedSearch {
  std::vector<TTEntry> tt;
  size_t ttSize = 1 << 22;
  std::atomic<bool> stop{false};
  std::chrono::steady_clock::time_point start{};
  SearchLimits limits{};
  std::atomic<std::uint64_t> nodes{0};
  uint8_t generation = 0;
  int softMs = 0;
  int hardMs = 0;
  SearchInfoFn infoFn;
};

class SearchWorker {
 public:
  SearchWorker(SharedSearch& shared, bool isMain);

  SearchResult run(Position& root);

 private:
  static constexpr int kMaxDepth = 64;
  static constexpr int kMateScore = 30000;
  static constexpr int kInf = 32000;
  static constexpr int kHistoryMax = 10000;

  int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool allowNull);
  int quiescence(Position& pos, int alpha, int beta, int ply);
  void orderMoves(Position& pos, std::vector<Move>& moves, const Move& ttMove, int ply);
  bool shouldStopHard() const;
  bool pastSoftLimit() const;
  bool isRepetition(const Position& pos) const;
  int scoreToTT(int score, int ply) const;
  int scoreFromTT(int score, int ply) const;
  void storeTT(Bitboard key, int score, int depth, int flag, const Move& best, int ply);
  bool probeTT(Bitboard key, int depth, int alpha, int beta, int ply, int& score,
               Move& ttMove);
  void clearLocal();
  void updateHistory(int& entry, int bonus);
  bool isKiller(const Move& m, int ply) const;
  Move pickSkillRootMove(Position& root, const std::vector<Move>& rootMoves,
                         const Move& best) const;

  SharedSearch& shared_;
  bool isMain_;

  std::vector<Bitboard> repHistory_;
  Move killers_[128][2]{};
  Move counters_[64][64]{};
  Move plyMove_[128]{};
  int history_[64][64]{};

  Move pvMove_{};
  int completedDepth_ = 0;
  int score_ = 0;
  int rootDepth_ = 0;
  std::uint64_t localNodes_ = 0;

  // Root move scores from the last completed ID iteration (main only).
  std::vector<std::pair<Move, int>> rootScores_;
};

class Searcher {
 public:
  SearchResult search(Position& root, SearchLimits limits);
  void setInfoCallback(SearchInfoFn fn);
  void newGame();
  void resizeHash(size_t mb);

 private:
  SharedSearch shared_;
};

Searcher& globalSearcher();

SearchResult findBestMove(const std::string& fen, SearchLimits limits);

// Resolve a UCI string to a fully flagged legal move in `pos`.
std::optional<Move> legalMoveFromUci(Position& pos, std::string_view uci);

}  // namespace chess
