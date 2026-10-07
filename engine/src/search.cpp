#include "chess/search.hpp"

#include "chess/book.hpp"
#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/timeman.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <thread>
#include <vector>

namespace chess {

namespace {

int mvvLva(const Position& pos, const Move& m) {
  static const int kVictim[7] = {0, 100, 200, 300, 400, 500, 600};
  static const int kAggressor[7] = {0, 6, 5, 4, 3, 2, 1};
  const Piece moving = pos.pieceOn(m.from);
  const int victim = kVictim[static_cast<int>(pieceType(m.captured))];
  const int aggr = kAggressor[static_cast<int>(pieceType(moving))];
  return victim * 10 - aggr;
}

bool isCapture(const Move& m) {
  return m.flag == MoveFlag::Capture || m.flag == MoveFlag::PromotionCapture ||
         m.flag == MoveFlag::EnPassant;
}

bool isQuiet(const Move& m) {
  return !isCapture(m) && m.promotion == PieceType::None;
}

int captureValue(const Move& m) {
  static const int kVal[] = {0, 100, 320, 330, 500, 900, 20000};
  if (m.promotion != PieceType::None) {
    return kVal[static_cast<int>(m.promotion)] - kVal[static_cast<int>(PieceType::Pawn)] +
           (m.captured != Piece::None ? kVal[static_cast<int>(pieceType(m.captured))] : 0);
  }
  if (m.captured == Piece::None) return 0;
  return kVal[static_cast<int>(pieceType(m.captured))];
}

constexpr int kRazorMargin[] = {0, 300, 450, 600};
constexpr int kReverseFutilityMargin[] = {0, 150, 300, 500, 700, 900, 1100};
constexpr int kFutilityMargin[] = {0, 150, 300, 450, 600, 750, 900};
constexpr int kSeeMargin[] = {0, 100, 200, 300, 400, 500};
constexpr int kDeltaMargin = 200;

int lmrReduction(int depth, int moveIndex) {
  if (depth < 3 || moveIndex < 3) return 0;
  const double r = 0.5 + std::log(static_cast<double>(depth)) *
                             std::log(static_cast<double>(moveIndex)) / 2.25;
  return std::max(0, static_cast<int>(r));
}

size_t ttEntriesForMb(size_t mb) {
  if (mb < 1) mb = 1;
  if (mb > 1024) mb = 1024;
  size_t entries = (mb * 1024ull * 1024ull) / sizeof(TTEntry);
  // Round down to power of two.
  size_t pow2 = 1;
  while (pow2 * 2 <= entries) pow2 *= 2;
  return std::max<size_t>(pow2, 1 << 16);
}

}  // namespace

std::optional<Move> legalMoveFromUci(Position& pos, std::string_view uci) {
  auto parsed = uciToMove(uci);
  if (!parsed) return std::nullopt;
  std::vector<Move> moves;
  MoveGen::generateLegal(pos, moves);
  for (const Move& m : moves) {
    if (m.from == parsed->from && m.to == parsed->to && m.promotion == parsed->promotion) {
      return m;
    }
  }
  return std::nullopt;
}

SearchWorker::SearchWorker(SharedSearch& shared, bool isMain)
    : shared_(shared), isMain_(isMain) {}

void SearchWorker::clearLocal() {
  std::fill(&killers_[0][0], &killers_[0][0] + 128 * 2, Move{});
  std::fill(&counters_[0][0], &counters_[0][0] + 64 * 64, Move{});
  std::fill(plyMove_, plyMove_ + 128, Move{});
  std::memset(history_, 0, sizeof(history_));
  localNodes_ = 0;
  completedDepth_ = 0;
  score_ = 0;
  rootDepth_ = 0;
  pvMove_ = {};
  rootScores_.clear();
}

void SearchWorker::updateHistory(int& entry, int bonus) {
  entry += bonus - entry * std::abs(bonus) / 512;
  if (entry > kHistoryMax) entry = kHistoryMax;
  if (entry < -kHistoryMax) entry = -kHistoryMax;
}

bool SearchWorker::isKiller(const Move& m, int ply) const {
  return killers_[ply][0] == m || killers_[ply][1] == m;
}

bool SearchWorker::shouldStopHard() const {
  if (shared_.stop.load(std::memory_order_relaxed)) return true;
  if (shared_.limits.nodes > 0 &&
      shared_.nodes.load(std::memory_order_relaxed) >= shared_.limits.nodes) {
    return true;
  }
  if (shared_.hardMs <= 0) return false;
  if ((localNodes_ & 2047) != 0) return false;
  const auto now = std::chrono::steady_clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - shared_.start).count();
  return ms >= shared_.hardMs;
}

bool SearchWorker::pastSoftLimit() const {
  if (shared_.softMs <= 0) return false;
  const auto now = std::chrono::steady_clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - shared_.start).count();
  return ms >= shared_.softMs;
}

bool SearchWorker::isRepetition(const Position& pos) const {
  const int half = pos.halfmoveClock();
  if (half < 4 || repHistory_.size() < 2) return false;
  const Bitboard key = pos.hash();
  const int start = static_cast<int>(repHistory_.size()) - 2;
  const int oldest = std::max(0, static_cast<int>(repHistory_.size()) - 1 - half);
  for (int i = start; i >= oldest; i -= 2) {
    if (repHistory_[static_cast<size_t>(i)] == key) return true;
  }
  return false;
}

int SearchWorker::scoreToTT(int score, int ply) const {
  if (score > kMateScore - 1000) return score + ply;
  if (score < -kMateScore + 1000) return score - ply;
  return score;
}

int SearchWorker::scoreFromTT(int score, int ply) const {
  if (score > kMateScore - 1000) return score - ply;
  if (score < -kMateScore + 1000) return score + ply;
  return score;
}

void SearchWorker::storeTT(Bitboard key, int score, int depth, int flag, const Move& best,
                           int ply) {
  TTEntry& slot = shared_.tt[key % shared_.ttSize];
  TTEntry e = slot;
  const bool sameKey = e.key == key;
  if (sameKey && e.generation == shared_.generation && e.depth > depth) return;
  if (!sameKey && e.generation == shared_.generation && e.depth > depth + 2) return;
  e.key = key;
  e.score = static_cast<int16_t>(scoreToTT(score, ply));
  e.depth = static_cast<int8_t>(depth);
  e.flag = static_cast<uint8_t>(flag);
  e.generation = shared_.generation;
  e.bestMove = best;
  slot = e;
}

bool SearchWorker::probeTT(Bitboard key, int depth, int alpha, int beta, int ply, int& score,
                           Move& ttMove) {
  const TTEntry e = shared_.tt[key % shared_.ttSize];
  if (e.key != key) return false;
  ttMove = e.bestMove;
  if (e.depth < depth) return false;
  const int s = scoreFromTT(e.score, ply);
  if (e.flag == 0) {
    score = s;
    return true;
  }
  if (e.flag == 1 && s <= alpha) {
    score = s;
    return true;
  }
  if (e.flag == 2 && s >= beta) {
    score = s;
    return true;
  }
  return false;
}

Move SearchWorker::pickSkillRootMove(Position& root, const std::vector<Move>& rootMoves,
                                     const Move& best) const {
  const int skill = shared_.limits.skillLevel;
  const int nCand = skillCandidateCount(skill);
  if (nCand <= 1 || rootMoves.empty()) return best;

  std::vector<std::pair<Move, int>> ranked = rootScores_;
  if (ranked.empty()) {
    // Fallback: static eval after each root move.
    for (const Move& m : rootMoves) {
      Undo undo;
      root.makeMove(m, undo);
      const int s = -evaluate(root);
      root.unmakeMove(m, undo);
      ranked.push_back({m, s});
    }
  }

  std::sort(ranked.begin(), ranked.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  const int take = std::min(nCand, static_cast<int>(ranked.size()));
  thread_local std::mt19937 rng{std::random_device{}()};
  // Bias toward better moves but allow weaker ones at low skill.
  std::vector<int> weights(static_cast<size_t>(take));
  int total = 0;
  for (int i = 0; i < take; ++i) {
    weights[static_cast<size_t>(i)] = (take - i) * (take - i);
    total += weights[static_cast<size_t>(i)];
  }
  std::uniform_int_distribution<int> dist(1, std::max(1, total));
  int pick = dist(rng);
  for (int i = 0; i < take; ++i) {
    pick -= weights[static_cast<size_t>(i)];
    if (pick <= 0) return ranked[static_cast<size_t>(i)].first;
  }
  return ranked.front().first;
}

SearchResult SearchWorker::run(Position& root) {
  clearLocal();
  repHistory_.clear();
  repHistory_.push_back(root.hash());

  std::vector<Move> rootMoves;
  MoveGen::generateLegal(root, rootMoves);
  if (rootMoves.empty()) {
    if (!isMain_) return {};
    return {"", root.inCheck(root.sideToMove()) ? -kMateScore : 0, 0, false};
  }

  pvMove_ = rootMoves.front();
  score_ = 0;
  completedDepth_ = 0;

  constexpr int kAspiration = 50;
  const int maxDepth = shared_.limits.maxDepth;

  for (int depth = 1; depth <= maxDepth; ++depth) {
    if (shared_.stop.load(std::memory_order_relaxed)) break;
    // Soft limit: do not start a new depth.
    if (depth > 1 && pastSoftLimit()) break;
    rootDepth_ = depth;
    if (isMain_) rootScores_.clear();

    int alpha = -kInf;
    int beta = kInf;
    const int asp = isMain_ ? kAspiration : kAspiration * 2;
    if (depth >= 4) {
      alpha = score_ - asp;
      beta = score_ + asp;
    }

    int score = 0;
    int failLow = 0;
    int failHigh = 0;
    while (true) {
      score = negamax(root, depth, alpha, beta, 0, true);
      if (shared_.stop.load(std::memory_order_relaxed)) break;
      if (score <= alpha) {
        ++failLow;
        alpha = (failLow >= 2) ? -kInf : score_ - asp * (1 << failLow);
        continue;
      }
      if (score >= beta) {
        ++failHigh;
        beta = (failHigh >= 2) ? kInf : score_ + asp * (1 << failHigh);
        continue;
      }
      break;
    }

    if (shared_.stop.load(std::memory_order_relaxed)) break;

    score_ = score;
    completedDepth_ = depth;
    if (isMain_ && shared_.infoFn) {
      shared_.infoFn(depth, score_, shared_.nodes.load(std::memory_order_relaxed));
    }
    if (std::abs(score) > kMateScore - 100) break;
  }

  if (!isMain_) return {};

  Move chosen = pvMove_;
  if (shared_.limits.skillLevel < 20) {
    chosen = pickSkillRootMove(root, rootMoves, pvMove_);
  }

  SearchResult result;
  result.bestMoveUci = moveToUci(chosen);
  result.scoreCp = score_;
  result.depthReached = completedDepth_;
  result.fromBook = false;
  return result;
}

int SearchWorker::negamax(Position& pos, int depth, int alpha, int beta, int ply,
                          bool allowNull) {
  if (shouldStopHard()) {
    shared_.stop.store(true, std::memory_order_relaxed);
    return evaluate(pos);
  }

  ++localNodes_;
  shared_.nodes.fetch_add(1, std::memory_order_relaxed);
  if (pos.isDraw() || isRepetition(pos)) return 0;
  if (ply >= 120) return evaluate(pos);

  const bool inCheck = pos.inCheck(pos.sideToMove());
  if (inCheck && ply < 2 * rootDepth_) ++depth;

  if (depth <= 0) return quiescence(pos, alpha, beta, ply);

  const bool pvNode = (beta - alpha) > 1;
  const Bitboard key = pos.hash();
  Move ttMove{};
  int ttScore = 0;
  if (probeTT(key, depth, alpha, beta, ply, ttScore, ttMove) && ply > 0) {
    return ttScore;
  }

  const int staticEval = evaluate(pos);

  if (!inCheck && depth <= 6 && ply > 0 && beta < kMateScore - 1000) {
    const int margin = kReverseFutilityMargin[depth];
    if (staticEval - margin >= beta) return staticEval;
  }

  if (!inCheck && depth <= 3 && ply > 0) {
    const int razor = staticEval + kRazorMargin[depth];
    if (razor <= alpha) {
      const int q = quiescence(pos, alpha, beta, ply);
      if (q <= alpha) return q;
    }
  }

  if (allowNull && !inCheck && !pvNode && depth >= 3 && beta < kInf - 100 &&
      pos.hasNonPawnMaterial(pos.sideToMove()) && staticEval >= beta) {
    const int R = 2 + depth / 4;
    NullUndo nundo;
    pos.makeNullMove(nundo);
    const int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
    pos.unmakeNullMove(nundo);
    if (shared_.stop.load(std::memory_order_relaxed)) return evaluate(pos);
    if (score >= beta) return score;
  }

  std::vector<Move> moves;
  MoveGen::generateLegal(pos, moves);
  if (moves.empty()) {
    if (inCheck) return -kMateScore + ply;
    return 0;
  }

  orderMoves(pos, moves, ttMove, ply);

  int best = -kInf;
  Move bestMove = moves.front();
  int flag = 1;
  int moveIndex = 0;
  int quietCount = 0;
  Move quietTried[64];
  int quietTriedCount = 0;
  const int lmpLimit = 3 + depth * depth;

  Undo undo;
  for (const Move& m : moves) {
    const bool quiet = isQuiet(m);
    const bool ttHit =
        m.from == ttMove.from && m.to == ttMove.to && m.promotion == ttMove.promotion;

    if (!pvNode && !inCheck && quiet && ply > 0 && depth <= 8 && moveIndex >= lmpLimit &&
        best > -kMateScore + 1000) {
      ++moveIndex;
      continue;
    }

    if (!pvNode && !inCheck && !quiet && !ttHit && ply > 0 && depth <= 5) {
      const int margin = kSeeMargin[std::min(depth, 5)];
      if (pos.see(m) < -margin) {
        ++moveIndex;
        continue;
      }
    }

    if (!inCheck && quiet && depth <= 6 && ply > 0 && best > -kMateScore + 1000) {
      const int futility = staticEval + kFutilityMargin[depth];
      if (futility <= alpha && quietCount >= 1) {
        ++moveIndex;
        continue;
      }
    }

    pos.makeMove(m, undo);
    repHistory_.push_back(pos.hash());
    plyMove_[ply] = m;
    const bool givesChk = pos.inCheck(pos.sideToMove());

    int score;
    if (moveIndex == 0) {
      score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
    } else {
      int reduction = 0;
      if (!pvNode && depth >= 3 && quiet && !givesChk && !inCheck && !ttHit &&
          !isKiller(m, ply)) {
        reduction = lmrReduction(depth, moveIndex);
        if (reduction >= depth) reduction = depth - 1;
      }

      score = -negamax(pos, depth - 1 - reduction, -alpha - 1, -alpha, ply + 1, true);
      if (score > alpha && reduction > 0) {
        score = -negamax(pos, depth - 1, -alpha - 1, -alpha, ply + 1, true);
      }
      if (score > alpha && score < beta) {
        score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
      }
    }

    repHistory_.pop_back();
    pos.unmakeMove(m, undo);
    if (quiet) {
      ++quietCount;
      if (quietTriedCount < 64) quietTried[quietTriedCount++] = m;
    }
    ++moveIndex;

    if (shared_.stop.load(std::memory_order_relaxed)) return evaluate(pos);

    if (ply == 0 && isMain_) {
      // Keep latest root move scores for skill selection.
      bool found = false;
      for (auto& rs : rootScores_) {
        if (rs.first == m) {
          rs.second = score;
          found = true;
          break;
        }
      }
      if (!found) rootScores_.push_back({m, score});
    }

    if (score > best) {
      best = score;
      bestMove = m;
    }
    if (score > alpha) {
      alpha = score;
      flag = 0;
      if (ply == 0 && isMain_) pvMove_ = m;
    }
    if (alpha >= beta) {
      flag = 2;
      if (quiet) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = m;
        const int bonus = depth * depth;
        updateHistory(history_[static_cast<int>(m.from)][static_cast<int>(m.to)], bonus);
        for (int i = 0; i < quietTriedCount - 1; ++i) {
          const Move& q = quietTried[i];
          updateHistory(history_[static_cast<int>(q.from)][static_cast<int>(q.to)], -bonus);
        }
        if (ply > 0) {
          const Move prev = plyMove_[ply - 1];
          if (prev.from != Square::None) {
            counters_[static_cast<int>(prev.from)][static_cast<int>(prev.to)] = m;
          }
        }
      }
      break;
    }
  }

  storeTT(key, best, depth, flag, bestMove, ply);
  return best;
}

int SearchWorker::quiescence(Position& pos, int alpha, int beta, int ply) {
  if (shouldStopHard()) {
    shared_.stop.store(true, std::memory_order_relaxed);
    return evaluate(pos);
  }
  ++localNodes_;
  shared_.nodes.fetch_add(1, std::memory_order_relaxed);
  if (pos.isDraw() || isRepetition(pos)) return 0;
  if (ply >= 120) return evaluate(pos);

  const bool inCheck = pos.inCheck(pos.sideToMove());
  int standPat = 0;
  if (!inCheck) {
    standPat = evaluate(pos);
    if (standPat >= beta) return beta;
    if (alpha < standPat) alpha = standPat;
  }

  std::vector<Move> moves;
  if (inCheck) {
    MoveGen::generateLegal(pos, moves);
    if (moves.empty()) return -kMateScore + ply;
  } else {
    MoveGen::generateLegalCaptures(pos, moves);
    moves.erase(std::remove_if(moves.begin(), moves.end(),
                               [&](const Move& m) { return pos.see(m) < -50; }),
                moves.end());
  }

  orderMoves(pos, moves, {}, ply);

  Undo undo;
  for (const Move& m : moves) {
    if (!inCheck) {
      if (standPat + captureValue(m) + kDeltaMargin <= alpha) continue;
    }

    pos.makeMove(m, undo);
    repHistory_.push_back(pos.hash());
    const int score = -quiescence(pos, -beta, -alpha, ply + 1);
    repHistory_.pop_back();
    pos.unmakeMove(m, undo);
    if (shared_.stop.load(std::memory_order_relaxed)) return evaluate(pos);
    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
  }
  return alpha;
}

void SearchWorker::orderMoves(Position& pos, std::vector<Move>& moves, const Move& ttMove,
                              int ply) {
  Move counter{};
  if (ply > 0) {
    const Move prev = plyMove_[ply - 1];
    if (prev.from != Square::None) {
      counter = counters_[static_cast<int>(prev.from)][static_cast<int>(prev.to)];
    }
  }

  auto scoreMove = [&](const Move& m) {
    if (m.from == ttMove.from && m.to == ttMove.to && m.promotion == ttMove.promotion)
      return 1000000;
    if (isCapture(m) || m.promotion != PieceType::None) {
      return 500000 + pos.see(m) + mvvLva(pos, m);
    }
    if (killers_[ply][0] == m) return 400000;
    if (killers_[ply][1] == m) return 390000;
    if (counter.from == m.from && counter.to == m.to &&
        counter.promotion == m.promotion)
      return 380000;
    return history_[static_cast<int>(m.from)][static_cast<int>(m.to)];
  };

  std::sort(moves.begin(), moves.end(),
            [&](const Move& a, const Move& b) { return scoreMove(a) > scoreMove(b); });
}

void Searcher::setInfoCallback(SearchInfoFn fn) { shared_.infoFn = std::move(fn); }

void Searcher::newGame() {
  ++shared_.generation;
  if (shared_.generation == 0) ++shared_.generation;
}

void Searcher::resizeHash(size_t mb) {
  const size_t entries = ttEntriesForMb(mb);
  if (entries != shared_.ttSize || shared_.tt.size() != entries) {
    shared_.ttSize = entries;
    shared_.tt.assign(entries, {});
  }
}

SearchResult Searcher::search(Position& root, SearchLimits limits) {
  applySkillLimits(limits);
  shared_.limits = limits;
  if (shared_.limits.maxDepth <= 0) shared_.limits.maxDepth = 64;
  if (shared_.limits.maxDepth > 64) shared_.limits.maxDepth = 64;
  if (shared_.limits.threads < 1) shared_.limits.threads = 1;
  if (shared_.limits.threads > 64) shared_.limits.threads = 64;

  if (limits.hashMb > 0) resizeHash(limits.hashMb);
  if (shared_.tt.empty()) resizeHash(16);

  if (limits.useBook) {
    if (auto bookMove = globalBook().probe(root)) {
      SearchResult r;
      r.bestMoveUci = moveToUci(*bookMove);
      r.scoreCp = 0;
      r.depthReached = 0;
      r.fromBook = true;
      return r;
    }
  }

  const TimeAllocation alloc = allocateTime(limits, root.sideToMove());
  shared_.softMs = alloc.softMs;
  shared_.hardMs = alloc.hardMs;

  shared_.stop.store(false, std::memory_order_relaxed);
  shared_.start = std::chrono::steady_clock::now();
  shared_.nodes.store(0, std::memory_order_relaxed);
  ++shared_.generation;
  if (shared_.generation == 0) ++shared_.generation;

  const int nThreads = shared_.limits.threads;
  std::vector<std::thread> helpers;
  helpers.reserve(static_cast<size_t>(nThreads - 1));

  for (int t = 1; t < nThreads; ++t) {
    helpers.emplace_back([this, root]() mutable {
      Position copy = root;
      SearchWorker worker(shared_, false);
      worker.run(copy);
    });
  }

  SearchWorker main(shared_, true);
  SearchResult result = main.run(root);

  shared_.stop.store(true, std::memory_order_relaxed);
  for (auto& th : helpers) {
    if (th.joinable()) th.join();
  }
  return result;
}

Searcher& globalSearcher() {
  static Searcher instance;
  return instance;
}

SearchResult findBestMove(const std::string& fen, SearchLimits limits) {
  Position pos;
  if (!pos.setFromFen(fen)) {
    return {"", 0, 0, false};
  }
  return globalSearcher().search(pos, limits);
}

}  // namespace chess
