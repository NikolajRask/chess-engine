#pragma once

#include "chess/movegen.hpp"
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
  int multiPv = 1;      // 1–5 ranked root lines
  bool useBook = true;
  bool infinite = false;
  size_t hashMb = 16;  // TT size hint
};

struct PvLine {
  std::string moveUci;
  int scoreCp = 0;
  std::optional<int> scoreMate;
  std::vector<std::string> pv;
};

struct SearchResult {
  std::string bestMoveUci;
  int scoreCp = 0;
  std::optional<int> scoreMate;  // signed plies to mate; nullopt if not mate
  int depthReached = 0;
  bool fromBook = false;
  std::uint64_t nodes = 0;
  int timeMs = 0;
  std::uint64_t nps = 0;
  std::vector<std::string> pv;
  std::vector<PvLine> multipv;  // ranked; [0] is the best line
  // Set when the root position is already terminal (mate/draw rules).
  bool gameOver = false;
  std::string outcome;  // e.g. "threefold", "stalemate", "checkmate", ...
};

struct TTEntry {
  Bitboard key = 0;
  int16_t score = 0;
  int8_t depth = -1;
  uint8_t flag = 0;  // 0 exact, 1 upper (alpha), 2 lower (beta)
  uint8_t generation = 0;
  Move bestMove{};
};

constexpr int kTTClusterSize = 3;

using SearchInfoFn = std::function<void(int depth, int scoreCp, std::uint64_t nodes)>;

struct SharedSearch {
  std::vector<TTEntry> tt;  // clusters of kTTClusterSize, contiguous
  size_t ttSize = 1 << 20;  // number of clusters (power of two)
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

  // `gameHistory` = Zobrist hashes from game start through the root (inclusive).
  SearchResult run(Position& root, const std::vector<Bitboard>& gameHistory = {});

 private:
  static constexpr int kMaxDepth = 64;
  static constexpr int kMateScore = 30000;
  static constexpr int kInf = 32000;
  static constexpr int kHistoryMax = 10000;

  int negamax(Position& pos, int depth, int alpha, int beta, int ply, bool allowNull,
              Move excludedMove = {});
  int quiescence(Position& pos, int alpha, int beta, int ply, int qsPly = 0);
  void orderMoves(Position& pos, MoveList& moves, const Move& ttMove, int ply);
  bool shouldStopHard() const;
  bool pastSoftLimit() const;
  bool isRepetition(const Position& pos) const;
  int scoreToTT(int score, int ply) const;
  int scoreFromTT(int score, int ply) const;
  void storeTT(Bitboard key, int score, int depth, int flag, const Move& best, int ply);
  // On hit: fills ttMove/ttValue/ttDepth/ttFlag. Returns true if a cutoff applies.
  bool probeTT(Bitboard key, int depth, int alpha, int beta, int ply, Move& ttMove,
               int& ttValue, int& ttDepth, int& ttFlag);
  void clearLocal();
  void updateHistory(int& entry, int bonus);
  bool isKiller(const Move& m, int ply) const;
  Move pickSkillRootMove(Position& root, const MoveList& rootMoves, const Move& best) const;
  bool isMultipvExcluded(const Move& m) const;
  void fillMultipv(Position& root, SearchResult& result);

  SharedSearch& shared_;
  bool isMain_;

  std::vector<Bitboard> repHistory_;
  Move killers_[128][2]{};
  Move counters_[64][64]{};
  Move plyMove_[128]{};
  int history_[64][64]{};
  // Continuation: previous move's to × current from × current to.
  int contHistory_[64][64][64]{};
  // Capture: moving piece type × to × captured piece type.
  int captureHistory_[7][64][7]{};

  Move pvMove_{};
  int completedDepth_ = 0;
  int score_ = 0;
  int rootDepth_ = 0;
  std::uint64_t localNodes_ = 0;

  // Root move scores from the last completed ID iteration (main only).
  std::vector<std::pair<Move, int>> rootScores_;
  // Root moves already claimed by earlier MultiPV lines (main only).
  std::vector<Move> multipvExclude_;
};

class Searcher {
 public:
  // `gameHistory` = Zobrist hashes from game start through the root (inclusive).
  // Empty → only the root hash is used (no prior-game repetition detection).
  SearchResult search(Position& root, SearchLimits limits,
                      const std::vector<Bitboard>& gameHistory = {});
  void setInfoCallback(SearchInfoFn fn);
  void newGame();
  void resizeHash(size_t mb);

 private:
  SharedSearch shared_;
};

Searcher& globalSearcher();

SearchResult findBestMove(const std::string& fen, SearchLimits limits);

}  // namespace chess
