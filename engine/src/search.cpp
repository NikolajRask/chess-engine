#include "chess/search.hpp"

#include "chess/book.hpp"
#include "chess/eval.hpp"
#include "chess/game.hpp"
#include "chess/movegen.hpp"
#include "chess/timeman.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
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

constexpr int kProbCutMargin = 180;

size_t ttClustersForMb(size_t mb) {
  if (mb < 1) mb = 1;
  if (mb > 1024) mb = 1024;
  const size_t entries = (mb * 1024ull * 1024ull) / sizeof(TTEntry);
  size_t clusters = entries / static_cast<size_t>(kTTClusterSize);
  // Round down to power of two.
  size_t pow2 = 1;
  while (pow2 * 2 <= clusters) pow2 *= 2;
  return std::max<size_t>(pow2, 1 << 14);
}

inline size_t ttClusterIndex(Bitboard key, size_t ttSize) {
  return static_cast<size_t>(key & (ttSize - 1));
}

const TTEntry* probeTTCluster(const SharedSearch& shared, Bitboard key) {
  const size_t base = ttClusterIndex(key, shared.ttSize) * static_cast<size_t>(kTTClusterSize);
  for (int i = 0; i < kTTClusterSize; ++i) {
    const TTEntry& e = shared.tt[base + static_cast<size_t>(i)];
    if (e.key == key) return &e;
  }
  return nullptr;
}

constexpr int kMateScoreAbs = 30000;

std::optional<int> mateScoreFromCp(int scoreCp) {
  if (std::abs(scoreCp) <= kMateScoreAbs - 100) return std::nullopt;
  const int plies = kMateScoreAbs - std::abs(scoreCp);
  return scoreCp > 0 ? plies : -plies;
}

std::vector<std::string> extractPv(Position pos, Move first, const SharedSearch& shared,
                                   int maxPly = 12) {
  std::vector<std::string> pv;
  if (first.from == Square::None) return pv;

  MoveList legal;
  MoveGen::generateLegal(pos, legal);
  bool firstOk = false;
  for (const Move& m : legal) {
    if (m.from == first.from && m.to == first.to && m.promotion == first.promotion &&
        m.flag == first.flag) {
      first = m;
      firstOk = true;
      break;
    }
    // TT / PV moves may differ only in flags; match by from/to/promo.
    if (m.from == first.from && m.to == first.to && m.promotion == first.promotion) {
      first = m;
      firstOk = true;
      break;
    }
  }
  if (!firstOk) return pv;

  std::vector<Bitboard> seen;
  seen.push_back(pos.hash());

  for (int ply = 0; ply < maxPly; ++ply) {
    Move pick = first;
    if (ply > 0) {
      const TTEntry* e = probeTTCluster(shared, pos.hash());
      if (!e || e->bestMove.from == Square::None) break;
      pick = e->bestMove;
      MoveGen::generateLegal(pos, legal);
      bool ok = false;
      for (const Move& m : legal) {
        if (m.from == pick.from && m.to == pick.to && m.promotion == pick.promotion) {
          pick = m;
          ok = true;
          break;
        }
      }
      if (!ok) break;
    }

    pv.push_back(moveToUci(pick));
    Undo undo;
    pos.makeMove(pick, undo);
    if (std::find(seen.begin(), seen.end(), pos.hash()) != seen.end()) break;
    seen.push_back(pos.hash());
    first = {};
  }
  return pv;
}

void fillSearchStats(SearchResult& result, const SharedSearch& shared) {
  const auto elapsed = std::chrono::steady_clock::now() - shared.start;
  result.timeMs =
      static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
  if (result.timeMs < 0) result.timeMs = 0;
  result.nodes = shared.nodes.load(std::memory_order_relaxed);
  result.nps = result.timeMs > 0 ? (result.nodes * 1000ull) / static_cast<std::uint64_t>(result.timeMs)
                                 : 0;
  result.scoreMate = mateScoreFromCp(result.scoreCp);
}

}  // namespace

SearchWorker::SearchWorker(SharedSearch& shared, bool isMain)
    : shared_(shared), isMain_(isMain) {}

void SearchWorker::clearLocal() {
  // Killers / ply stack reset each search; history survives via loadHeuristics().
  std::fill(&killers_[0][0], &killers_[0][0] + 128 * 2, Move{});
  std::fill(plyMove_, plyMove_ + 128, Move{});
  std::memset(evalStack_, 0, sizeof(evalStack_));
  localNodes_ = 0;
  completedDepth_ = 0;
  score_ = 0;
  rootDepth_ = 0;
  pvMove_ = {};
  rootScores_.clear();
  multipvExclude_.clear();
}

void SearchWorker::loadHeuristics() {
  const SearchHeuristics& h = shared_.heuristics;
  std::memcpy(counters_, h.counters, sizeof(counters_));
  std::memcpy(history_, h.history, sizeof(history_));
  std::memcpy(contHistory_, h.contHistory, sizeof(contHistory_));
  std::memcpy(captureHistory_, h.captureHistory, sizeof(captureHistory_));
}

void SearchWorker::saveHeuristics() {
  SearchHeuristics& h = shared_.heuristics;
  std::memcpy(h.counters, counters_, sizeof(counters_));
  std::memcpy(h.history, history_, sizeof(history_));
  std::memcpy(h.contHistory, contHistory_, sizeof(contHistory_));
  std::memcpy(h.captureHistory, captureHistory_, sizeof(captureHistory_));
}

int SearchWorker::quietHistoryScore(const Move& m, int ply) const {
  int score = history_[static_cast<int>(m.from)][static_cast<int>(m.to)];
  if (ply > 0) {
    const Move prev = plyMove_[ply - 1];
    if (prev.from != Square::None) {
      score += contHistory_[static_cast<int>(prev.to)][static_cast<int>(m.from)]
                           [static_cast<int>(m.to)];
    }
  }
  return score;
}

bool SearchWorker::isMultipvExcluded(const Move& m) const {
  for (const Move& ex : multipvExclude_) {
    if (m.from == ex.from && m.to == ex.to && m.promotion == ex.promotion) return true;
  }
  return false;
}

void SearchWorker::fillMultipv(Position& root, SearchResult& result) {
  result.multipv.clear();
  if (pvMove_.from == Square::None) return;

  int want = shared_.limits.multiPv;
  if (want < 1) want = 1;
  if (want > 5) want = 5;

  MoveList rootMoves;
  MoveGen::generateLegal(root, rootMoves);
  if (rootMoves.empty()) return;

  const int depth = std::max(1, completedDepth_);
  multipvExclude_.clear();

  const int savedSoft = shared_.softMs;
  const int savedHard = shared_.hardMs;
  if (want > 1) {
    // Main ID often exhausts the hard limit; give secondary lines a fresh budget.
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - shared_.start)
                             .count();
    const int perLine =
        std::max(80, shared_.limits.timeMs > 0 ? shared_.limits.timeMs / 2 : 200);
    const int extra = perLine * (want - 1);
    shared_.softMs = static_cast<int>(elapsed) + extra;
    shared_.hardMs = static_cast<int>(elapsed) + extra + perLine;
    shared_.stop.store(false, std::memory_order_relaxed);
  }

  for (int line = 0; line < want; ++line) {
    Move best{};
    int bestScore = 0;

    if (line == 0) {
      best = pvMove_;
      bestScore = score_;
    } else {
      shared_.stop.store(false, std::memory_order_relaxed);
      rootDepth_ = depth;
      bestScore = negamax(root, depth, -kInf, kInf, 0, true);
      best = pvMove_;
    }

    if (best.from == Square::None || isMultipvExcluded(best)) break;

    // Prefer a fully flagged legal move from the root list.
    for (const Move& m : rootMoves) {
      if (m.from == best.from && m.to == best.to && m.promotion == best.promotion) {
        best = m;
        break;
      }
    }

    PvLine pvLine;
    pvLine.moveUci = moveToUci(best);
    pvLine.scoreCp = bestScore;
    pvLine.scoreMate = mateScoreFromCp(bestScore);
    pvLine.pv = extractPv(root, best, shared_);
    if (pvLine.pv.empty()) pvLine.pv = {pvLine.moveUci};
    result.multipv.push_back(std::move(pvLine));
    multipvExclude_.push_back(best);

    // Remaining legal moves?
    bool anyLeft = false;
    for (const Move& m : rootMoves) {
      if (!isMultipvExcluded(m)) {
        anyLeft = true;
        break;
      }
    }
    if (!anyLeft) break;
  }

  multipvExclude_.clear();
  shared_.softMs = savedSoft;
  shared_.hardMs = savedHard;
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
  // repHistory_.back() is the current position (pushed by the caller). A repeat
  // of the same STM needs at least 4 plies, so the first candidate is size-5.
  const int half = pos.halfmoveClock();
  if (half < 4 || repHistory_.size() < 5) return false;
  const Bitboard key = pos.hash();
  const int start = static_cast<int>(repHistory_.size()) - 5;
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
  const size_t base =
      ttClusterIndex(key, shared_.ttSize) * static_cast<size_t>(kTTClusterSize);
  TTEntry* cluster = &shared_.tt[base];

  int replace = -1;
  int worst = 100000;
  for (int i = 0; i < kTTClusterSize; ++i) {
    TTEntry& e = cluster[i];
    if (e.key == key) {
      replace = i;
      break;
    }
    // Prefer empty, then older generation, then shallower.
    int quality = static_cast<int>(e.depth);
    if (e.generation == shared_.generation) quality += 512;
    if (e.depth < 0 || e.key == 0) quality = -1000;
    if (quality < worst) {
      worst = quality;
      replace = i;
    }
  }
  if (replace < 0) replace = 0;

  TTEntry& dest = cluster[replace];
  if (dest.key == key && dest.generation == shared_.generation && dest.depth > depth) {
    if (best.from != Square::None) dest.bestMove = best;
    return;
  }
  if (dest.key != key && dest.generation == shared_.generation && dest.depth > depth + 2 &&
      dest.key != 0 && dest.depth >= 0) {
    return;
  }

  const Move keepMove =
      (best.from == Square::None && dest.key == key) ? dest.bestMove : best;
  dest.key = key;
  dest.score = static_cast<int16_t>(scoreToTT(score, ply));
  dest.depth = static_cast<int8_t>(depth);
  dest.flag = static_cast<uint8_t>(flag);
  dest.generation = shared_.generation;
  dest.bestMove = keepMove;
}

bool SearchWorker::probeTT(Bitboard key, int depth, int alpha, int beta, int ply, Move& ttMove,
                           int& ttValue, int& ttDepth, int& ttFlag) {
  const TTEntry* hit = probeTTCluster(shared_, key);
  if (!hit) return false;
  ttMove = hit->bestMove;
  ttValue = scoreFromTT(hit->score, ply);
  ttDepth = hit->depth;
  ttFlag = hit->flag;
  if (hit->depth < depth) return false;
  if (hit->flag == 0) return true;
  if (hit->flag == 1 && ttValue <= alpha) return true;
  if (hit->flag == 2 && ttValue >= beta) return true;
  return false;
}

Move SearchWorker::pickSkillRootMove(Position& root, const MoveList& rootMoves,
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

SearchResult SearchWorker::run(Position& root, const std::vector<Bitboard>& gameHistory) {
  clearLocal();
  loadHeuristics();
  repHistory_.clear();
  if (!gameHistory.empty()) {
    repHistory_ = gameHistory;
    if (repHistory_.back() != root.hash()) {
      repHistory_.push_back(root.hash());
    }
  } else {
    repHistory_.push_back(root.hash());
  }

  if (isMain_) {
    const GameOutcome outcome = gameOutcome(root, repHistory_);
    if (isTerminalOutcome(outcome)) {
      SearchResult terminal;
      terminal.gameOver = true;
      terminal.outcome = std::string(outcomeToString(outcome));
      if (outcome == GameOutcome::Checkmate) {
        terminal.scoreCp = -kMateScore;
        terminal.scoreMate = mateScoreFromCp(terminal.scoreCp);
      } else {
        terminal.scoreCp = 0;
      }
      return terminal;
    }
  }

  MoveList rootMoves;
  MoveGen::generateLegal(root, rootMoves);
  if (rootMoves.empty()) {
    if (!isMain_) return {};
    SearchResult empty;
    empty.gameOver = true;
    const bool mate = root.inCheck(root.sideToMove());
    empty.outcome = mate ? "checkmate" : "stalemate";
    empty.scoreCp = mate ? -kMateScore : 0;
    empty.scoreMate = mateScoreFromCp(empty.scoreCp);
    return empty;
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
        // Unstable: spend more time when aspiration fails low.
        if (isMain_ && shared_.initialSoftMs > 0) {
          const int extend = std::max(20, shared_.initialSoftMs / 4);
          const int softCap = shared_.initialSoftMs + shared_.initialSoftMs;
          shared_.softMs = std::min(softCap, shared_.softMs + extend);
          if (shared_.hardMs > 0) {
            shared_.hardMs =
                std::min(shared_.hardMs + extend, shared_.softMs + shared_.initialSoftMs / 2);
          }
        }
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
    // Only stop once the mate fits in the ID horizon so longer mates (e.g. M15
    // found via extensions) can still be refined toward the true short mate.
    if (std::abs(score) > kMateScore - 100) {
      const int matePlies = kMateScore - std::abs(score);
      if (matePlies <= depth) break;
    }
  }

  if (isMain_) saveHeuristics();
  if (!isMain_) return {};

  SearchResult result;
  result.scoreCp = score_;
  result.scoreMate = mateScoreFromCp(score_);
  result.depthReached = completedDepth_;
  result.fromBook = false;
  // PV from the true best line (pvMove_), even if skill played a different move.
  const Move trueBest = pvMove_;
  result.pv = extractPv(root, trueBest, shared_);

  fillMultipv(root, result);
  // Secondary MultiPV searches overwrite pvMove_; restore the ID best.
  pvMove_ = trueBest;

  Move chosen = trueBest;
  if (shared_.limits.skillLevel < 20) {
    chosen = pickSkillRootMove(root, rootMoves, trueBest);
  }
  result.bestMoveUci = moveToUci(chosen);
  if (result.pv.empty() && chosen.from != Square::None) {
    result.pv = {moveToUci(chosen)};
  }
  if (result.multipv.empty() && !result.bestMoveUci.empty()) {
    PvLine line;
    line.moveUci = moveToUci(trueBest);
    line.scoreCp = result.scoreCp;
    line.scoreMate = result.scoreMate;
    line.pv = result.pv;
    result.multipv.push_back(std::move(line));
  }
  return result;
}

int SearchWorker::negamax(Position& pos, int depth, int alpha, int beta, int ply,
                          bool allowNull, Move excludedMove) {
  if (shouldStopHard()) {
    shared_.stop.store(true, std::memory_order_relaxed);
    return evaluate(pos);
  }

  ++localNodes_;
  shared_.nodes.fetch_add(1, std::memory_order_relaxed);
  // 50-move / material draws always; 2-fold only below root (root 2-fold is not yet claimable).
  if (pos.isDraw() || (ply > 0 && isRepetition(pos))) return 0;
  if (ply >= 120) return evaluate(pos);

  // Mate-distance pruning: cannot improve on a shorter mate already in the window.
  alpha = std::max(alpha, -kMateScore + ply);
  beta = std::min(beta, kMateScore - ply - 1);
  if (alpha >= beta) return alpha;

  const bool excluding = excludedMove.from != Square::None;
  const bool inCheck = pos.inCheck(pos.sideToMove());
  if (inCheck && ply < 2 * rootDepth_) ++depth;

  if (depth <= 0) return quiescence(pos, alpha, beta, ply);

  const bool pvNode = (beta - alpha) > 1;
  const Bitboard key = pos.hash();
  Move ttMove{};
  int ttValue = 0;
  int ttDepth = -1;
  int ttFlag = 0;
  // Excluded-move searches must not take TT cutoffs (same key, incomplete tree).
  if (probeTT(key, depth, alpha, beta, ply, ttMove, ttValue, ttDepth, ttFlag) && ply > 0 &&
      !excluding) {
    return ttValue;
  }

  // Internal iterative reduction: no TT move → search shallower first.
  if (!excluding && ttMove.from == Square::None && depth >= 4) {
    --depth;
  }

  const int staticEval = evaluate(pos);
  if (ply < 128) evalStack_[ply] = staticEval;
  const bool improving =
      ply >= 2 && !inCheck && staticEval > evalStack_[ply - 2];

  if (!inCheck && depth <= 6 && ply > 0 && beta < kMateScore - 1000) {
    int margin = kReverseFutilityMargin[depth];
    if (improving) margin = margin * 3 / 4;
    if (staticEval - margin >= beta) return staticEval;
  }

  if (!inCheck && depth <= 3 && ply > 0 && beta < kMateScore - 1000) {
    const int razor = staticEval + kRazorMargin[depth];
    if (razor <= alpha) {
      const int q = quiescence(pos, alpha, beta, ply);
      if (q <= alpha) return q;
    }
  }

  if (allowNull && !excluding && !inCheck && !pvNode && depth >= 3 && beta < kInf - 100 &&
      pos.hasNonPawnMaterial(pos.sideToMove()) && staticEval >= beta) {
    int R = 2 + depth / 4;
    if (improving) ++R;
    NullUndo nundo;
    pos.makeNullMove(nundo);
    const int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
    pos.unmakeNullMove(nundo);
    if (shared_.stop.load(std::memory_order_relaxed)) return evaluate(pos);
    // Do not trust unverified mate-score fail-highs from null move.
    if (score >= beta && std::abs(score) <= kMateScore - 1000) return score;
  }

  // ProbCut: reduced-depth capture search against a raised beta.
  if (!pvNode && !excluding && !inCheck && depth >= 5 && std::abs(beta) < kMateScore - 1000 &&
      staticEval >= beta + kProbCutMargin / 2) {
    const int probBeta = beta + kProbCutMargin;
    const int rDepth = depth - 3 - depth / 4;
    MoveList caps;
    MoveGen::generateLegalCaptures(pos, caps);
    orderMoves(pos, caps, ttMove, ply);
    Undo undoPc;
    for (const Move& m : caps) {
      if (pos.see(m) < kProbCutMargin / 2) continue;
      pos.makeMove(m, undoPc);
      repHistory_.push_back(pos.hash());
      plyMove_[ply] = m;
      const int score =
          -negamax(pos, std::max(1, rDepth), -probBeta, -probBeta + 1, ply + 1, true);
      repHistory_.pop_back();
      pos.unmakeMove(m, undoPc);
      if (shared_.stop.load(std::memory_order_relaxed)) return evaluate(pos);
      if (score >= probBeta) return score;
    }
  }

  MoveList moves;
  MoveGen::generateLegal(pos, moves);
  if (moves.empty()) {
    if (inCheck) return -kMateScore + ply;
    return 0;
  }

  orderMoves(pos, moves, ttMove, ply);

  // Singular extension: if the TT move is clearly better than alternatives, extend it.
  int extension = 0;
  if (!excluding && ply > 0 && depth >= 8 && ttMove.from != Square::None &&
      (ttFlag == 0 || ttFlag == 2) && ttDepth >= depth - 3 &&
      std::abs(ttValue) < kMateScore - 1000) {
    Move ttLegal{};
    for (const Move& m : moves) {
      if (m.from == ttMove.from && m.to == ttMove.to && m.promotion == ttMove.promotion) {
        ttLegal = m;
        break;
      }
    }
    if (ttLegal.from != Square::None) {
      const int margin = 2 * depth;
      const int singularBeta = ttValue - margin;
      const int verify =
          negamax(pos, depth / 2, singularBeta - 1, singularBeta, ply, false, ttLegal);
      if (shared_.stop.load(std::memory_order_relaxed)) return evaluate(pos);
      if (verify < singularBeta) {
        extension = 1;
      } else if (verify >= beta) {
        return verify;  // multi-cut
      }
    }
  }

  int best = -kInf;
  Move bestMove{};
  for (const Move& m : moves) {
    if (excluding && m == excludedMove) continue;
    if (ply == 0 && isMultipvExcluded(m)) continue;
    bestMove = m;
    break;
  }
  if (bestMove.from == Square::None) {
    // All moves excluded (MultiPV / singular); treat as fail-low.
    return alpha;
  }
  int flag = 1;
  int moveIndex = 0;
  int quietCount = 0;
  Move quietTried[64];
  int quietTriedCount = 0;
  Move captureTried[64];
  int captureTriedCount = 0;
  const int lmpLimit = 3 + depth * depth;

  auto moveGivesCheck = [&](const Move& mv) -> bool {
    Undo probe;
    pos.makeMove(mv, probe);
    const bool chk = pos.inCheck(pos.sideToMove());
    pos.unmakeMove(mv, probe);
    return chk;
  };

  Undo undo;
  for (const Move& m : moves) {
    if (excluding && m == excludedMove) continue;
    if (ply == 0 && isMultipvExcluded(m)) continue;

    const bool quiet = isQuiet(m);
    const bool capture = isCapture(m);
    const bool ttHit =
        m.from == ttMove.from && m.to == ttMove.to && m.promotion == ttMove.promotion;

    const bool mayLmp = !pvNode && !inCheck && quiet && ply > 0 && depth <= 8 &&
                        moveIndex >= lmpLimit && best > -kMateScore + 1000;
    int futMargin = kFutilityMargin[depth];
    if (improving) futMargin = futMargin * 3 / 4;
    const bool mayFutile = !inCheck && quiet && depth <= 6 && ply > 0 &&
                           best > -kMateScore + 1000 && quietCount >= 1 &&
                           staticEval + futMargin <= alpha;
    if ((mayLmp || mayFutile) && !moveGivesCheck(m)) {
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

    pos.makeMove(m, undo);
    repHistory_.push_back(pos.hash());
    plyMove_[ply] = m;
    const bool givesChk = pos.inCheck(pos.sideToMove());

    int score;
    if (moveIndex == 0) {
      const int newDepth = depth - 1 + (ttHit ? extension : 0);
      score = -negamax(pos, newDepth, -beta, -alpha, ply + 1, true);
    } else {
      int reduction = 0;
      if (!pvNode && depth >= 3 && quiet && !givesChk && !inCheck && !ttHit &&
          !isKiller(m, ply)) {
        reduction = lmrReduction(depth, moveIndex);
        if (!improving) ++reduction;
        const int hist = quietHistoryScore(m, ply);
        if (hist > 2000) --reduction;
        else if (hist < -1000) ++reduction;
        if (reduction < 0) reduction = 0;
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
    } else if (capture) {
      if (captureTriedCount < 64) captureTried[captureTriedCount++] = m;
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
      const int bonus = depth * depth;
      if (quiet) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = m;
        updateHistory(history_[static_cast<int>(m.from)][static_cast<int>(m.to)], bonus);
        for (int i = 0; i < quietTriedCount - 1; ++i) {
          const Move& q = quietTried[i];
          updateHistory(history_[static_cast<int>(q.from)][static_cast<int>(q.to)], -bonus);
        }
        if (ply > 0) {
          const Move prev = plyMove_[ply - 1];
          if (prev.from != Square::None) {
            counters_[static_cast<int>(prev.from)][static_cast<int>(prev.to)] = m;
            const int pTo = static_cast<int>(prev.to);
            updateHistory(contHistory_[pTo][static_cast<int>(m.from)][static_cast<int>(m.to)],
                          bonus);
            for (int i = 0; i < quietTriedCount - 1; ++i) {
              const Move& q = quietTried[i];
              updateHistory(
                  contHistory_[pTo][static_cast<int>(q.from)][static_cast<int>(q.to)], -bonus);
            }
          }
        }
      } else if (capture) {
        const Piece moving = pos.pieceOn(m.from);
        const int pt = static_cast<int>(pieceType(moving));
        const int cap = static_cast<int>(pieceType(m.captured));
        updateHistory(captureHistory_[pt][static_cast<int>(m.to)][cap], bonus);
        for (int i = 0; i < captureTriedCount - 1; ++i) {
          const Move& c = captureTried[i];
          const Piece cMoving = pos.pieceOn(c.from);
          const int cPt = static_cast<int>(pieceType(cMoving));
          const int cCap = static_cast<int>(pieceType(c.captured));
          updateHistory(captureHistory_[cPt][static_cast<int>(c.to)][cCap], -bonus);
        }
      }
      break;
    }
  }

  // Only the TT move was legal (excluded); fail low for singularity verification.
  if (best == -kInf) return alpha;

  if (!excluding) storeTT(key, best, depth, flag, bestMove, ply);
  return best;
}

int SearchWorker::quiescence(Position& pos, int alpha, int beta, int ply, int qsPly) {
  if (shouldStopHard()) {
    shared_.stop.store(true, std::memory_order_relaxed);
    return evaluate(pos);
  }
  ++localNodes_;
  shared_.nodes.fetch_add(1, std::memory_order_relaxed);
  if (pos.isDraw() || (ply > 0 && isRepetition(pos))) return 0;
  if (ply >= 120) return evaluate(pos);

  const Bitboard key = pos.hash();
  Move ttMove{};
  int ttValue = 0;
  int ttDepth = -1;
  int ttFlag = 0;
  if (probeTT(key, 0, alpha, beta, ply, ttMove, ttValue, ttDepth, ttFlag) && ply > 0) {
    return ttValue;
  }

  const bool inCheck = pos.inCheck(pos.sideToMove());
  int standPat = 0;
  if (!inCheck) {
    standPat = evaluate(pos);
    if (standPat >= beta) {
      storeTT(key, standPat, 0, 2, ttMove, ply);
      return beta;
    }
    if (alpha < standPat) alpha = standPat;
  }

  MoveList moves;
  if (inCheck) {
    MoveGen::generateLegal(pos, moves);
    if (moves.empty()) return -kMateScore + ply;
  } else {
    MoveGen::generateLegalCaptures(pos, moves);
    int n = 0;
    for (int i = 0; i < moves.count(); ++i) {
      if (pos.see(moves[i]) >= -50) moves[n++] = moves[i];
    }
    moves.size = n;

    // Quiet checks for several QS plies so perpetuals can reach a 2-fold.
    if (qsPly < 6) {
      MoveList all;
      MoveGen::generateLegal(pos, all);
      Undo probe;
      for (int i = 0; i < all.count(); ++i) {
        const Move& m = all[i];
        if (!isQuiet(m)) continue;
        pos.makeMove(m, probe);
        const bool givesChk = pos.inCheck(pos.sideToMove());
        pos.unmakeMove(m, probe);
        if (givesChk) moves.push(m);
      }
    }
  }

  orderMoves(pos, moves, ttMove, ply);

  int best = inCheck ? -kInf : standPat;
  Move bestMove = ttMove;
  int flag = 1;
  Undo undo;
  for (const Move& m : moves) {
    // Delta prune captures only; quiet checks must stay.
    if (!inCheck && !isQuiet(m)) {
      if (standPat + captureValue(m) + kDeltaMargin <= alpha) continue;
    }

    pos.makeMove(m, undo);
    repHistory_.push_back(pos.hash());
    const int score = -quiescence(pos, -beta, -alpha, ply + 1, qsPly + 1);
    repHistory_.pop_back();
    pos.unmakeMove(m, undo);
    if (shared_.stop.load(std::memory_order_relaxed)) return evaluate(pos);
    if (score > best) {
      best = score;
      bestMove = m;
    }
    if (score >= beta) {
      storeTT(key, score, 0, 2, m, ply);
      return beta;
    }
    if (score > alpha) {
      alpha = score;
      flag = 0;
    }
  }
  storeTT(key, best, 0, flag, bestMove, ply);
  return alpha;
}

void SearchWorker::orderMoves(Position& pos, MoveList& moves, const Move& ttMove, int ply) {
  Move counter{};
  int prevTo = -1;
  if (ply > 0) {
    const Move prev = plyMove_[ply - 1];
    if (prev.from != Square::None) {
      counter = counters_[static_cast<int>(prev.from)][static_cast<int>(prev.to)];
      prevTo = static_cast<int>(prev.to);
    }
  }

  auto scoreMove = [&](const Move& m) {
    if (m.from == ttMove.from && m.to == ttMove.to && m.promotion == ttMove.promotion)
      return 1000000;
    if (isCapture(m) || m.promotion != PieceType::None) {
      int hist = 0;
      if (isCapture(m)) {
        const Piece moving = pos.pieceOn(m.from);
        const int pt = static_cast<int>(pieceType(moving));
        const int cap = static_cast<int>(pieceType(m.captured));
        hist = captureHistory_[pt][static_cast<int>(m.to)][cap];
      }
      return 500000 + pos.see(m) + mvvLva(pos, m) + hist;
    }
    if (killers_[ply][0] == m) return 400000;
    if (killers_[ply][1] == m) return 390000;
    if (counter.from == m.from && counter.to == m.to &&
        counter.promotion == m.promotion)
      return 380000;
    int score = history_[static_cast<int>(m.from)][static_cast<int>(m.to)];
    if (prevTo >= 0) {
      score += contHistory_[prevTo][static_cast<int>(m.from)][static_cast<int>(m.to)];
    }
    return score;
  };

  std::sort(moves.begin(), moves.end(),
            [&](const Move& a, const Move& b) { return scoreMove(a) > scoreMove(b); });
}

void Searcher::setInfoCallback(SearchInfoFn fn) { shared_.infoFn = std::move(fn); }

void Searcher::newGame() {
  ++shared_.generation;
  if (shared_.generation == 0) ++shared_.generation;
  shared_.heuristics.clear();
}

void Searcher::resizeHash(size_t mb) {
  const size_t clusters = ttClustersForMb(mb);
  const size_t entries = clusters * static_cast<size_t>(kTTClusterSize);
  if (clusters != shared_.ttSize || shared_.tt.size() != entries) {
    shared_.ttSize = clusters;
    shared_.tt.assign(entries, {});
  }
}

SearchResult Searcher::search(Position& root, SearchLimits limits,
                              const std::vector<Bitboard>& gameHistory) {
  std::lock_guard<std::mutex> lock(searchMutex_);

  applySkillLimits(limits);
  shared_.limits = limits;
  if (shared_.limits.maxDepth <= 0) shared_.limits.maxDepth = 64;
  if (shared_.limits.maxDepth > 64) shared_.limits.maxDepth = 64;
  if (shared_.limits.threads < 1) shared_.limits.threads = 1;
  if (shared_.limits.threads > 64) shared_.limits.threads = 64;
  if (shared_.limits.multiPv < 1) shared_.limits.multiPv = 1;
  if (shared_.limits.multiPv > 5) shared_.limits.multiPv = 5;

  if (limits.hashMb > 0) resizeHash(limits.hashMb);
  if (shared_.tt.empty()) resizeHash(16);

  // Detect terminal roots before book / search so draws are not "played".
  {
    std::vector<Bitboard> hist = gameHistory;
    if (hist.empty() || hist.back() != root.hash()) hist.push_back(root.hash());
    const GameOutcome outcome = gameOutcome(root, hist);
    if (isTerminalOutcome(outcome)) {
      SearchResult terminal;
      terminal.gameOver = true;
      terminal.outcome = std::string(outcomeToString(outcome));
      if (outcome == GameOutcome::Checkmate) {
        terminal.scoreCp = -30000;
        terminal.scoreMate = mateScoreFromCp(terminal.scoreCp);
      } else {
        terminal.scoreCp = 0;
      }
      return terminal;
    }
  }

  if (limits.useBook) {
    if (auto bookMove = globalBook().probe(root)) {
      SearchResult r;
      r.bestMoveUci = moveToUci(*bookMove);
      r.scoreCp = 0;
      r.depthReached = 0;
      r.fromBook = true;
      r.pv = {r.bestMoveUci};
      PvLine line;
      line.moveUci = r.bestMoveUci;
      line.scoreCp = 0;
      line.pv = r.pv;
      r.multipv.push_back(std::move(line));
      return r;
    }
  }

  const TimeAllocation alloc = allocateTime(limits, root.sideToMove());
  shared_.softMs = alloc.softMs;
  shared_.hardMs = alloc.hardMs;
  shared_.initialSoftMs = alloc.softMs;

  shared_.stop.store(false, std::memory_order_relaxed);
  shared_.start = std::chrono::steady_clock::now();
  shared_.nodes.store(0, std::memory_order_relaxed);
  ++shared_.generation;
  if (shared_.generation == 0) ++shared_.generation;

  const int nThreads = shared_.limits.threads;
  std::vector<std::thread> helpers;
  helpers.reserve(static_cast<size_t>(nThreads - 1));

  // SearchWorker is ~1MB (history tables). Heap-allocate so httplib / std::thread
  // stacks (often 512KB on macOS) do not overflow.
  for (int t = 1; t < nThreads; ++t) {
    helpers.emplace_back([this, root, gameHistory]() mutable {
      Position copy = root;
      auto worker = std::make_unique<SearchWorker>(shared_, false);
      worker->run(copy, gameHistory);
    });
  }

  // Run the main worker on a normal-stack thread too. httplib request threads are
  // ~512KB on macOS; MoveList frames (~3KB) + check extensions can blow that stack
  // deep into a game even though SearchWorker itself is on the heap.
  SearchResult result;
  std::exception_ptr eptr;
  std::thread mainTh([this, &root, &gameHistory, &result, &eptr]() {
    try {
      auto mainWorker = std::make_unique<SearchWorker>(shared_, true);
      result = mainWorker->run(root, gameHistory);
    } catch (...) {
      eptr = std::current_exception();
    }
  });
  mainTh.join();

  shared_.stop.store(true, std::memory_order_relaxed);
  for (auto& th : helpers) {
    if (th.joinable()) th.join();
  }
  if (eptr) std::rethrow_exception(eptr);
  fillSearchStats(result, shared_);
  return result;
}

Searcher& globalSearcher() {
  static Searcher instance;
  return instance;
}

SearchResult findBestMove(const std::string& fen, SearchLimits limits) {
  Position pos;
  if (!pos.setFromFen(fen)) {
    return {};
  }
  return globalSearcher().search(pos, limits);
}

}  // namespace chess
