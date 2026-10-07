#include "chess/search.hpp"

#include "chess/eval.hpp"
#include "chess/movegen.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

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

}  // namespace

bool Searcher::shouldStop() const {
  if (limits_.timeMs <= 0) return false;
  if ((nodes_ & 2047) != 0) return stop_.load();
  const auto now = std::chrono::steady_clock::now();
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
  return ms >= limits_.timeMs;
}

int Searcher::scoreToTT(int score, int ply) const {
  if (score > kMateScore - 1000) return score + ply;
  if (score < -kMateScore + 1000) return score - ply;
  return score;
}

int Searcher::scoreFromTT(int score, int ply) const {
  if (score > kMateScore - 1000) return score - ply;
  if (score < -kMateScore + 1000) return score + ply;
  return score;
}

void Searcher::storeTT(Bitboard key, int score, int depth, int flag,
                       const Move& best, int ply) {
  TTEntry& e = tt_[key % kTTSize];
  if (e.key == key && e.depth > depth) return;
  e.key = key;
  e.score = static_cast<int16_t>(scoreToTT(score, ply));
  e.depth = static_cast<int8_t>(depth);
  e.flag = static_cast<uint8_t>(flag);
  e.bestMove = best;
}

bool Searcher::probeTT(Bitboard key, int depth, int alpha, int beta, int ply,
                       int& score, Move& ttMove) {
  TTEntry& e = tt_[key % kTTSize];
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

SearchResult Searcher::search(Position& root, SearchLimits limits) {
  limits_ = limits;
  if (limits_.maxDepth <= 0) limits_.maxDepth = kMaxDepth;
  if (limits_.maxDepth > kMaxDepth) limits_.maxDepth = kMaxDepth;
  stop_ = false;
  start_ = std::chrono::steady_clock::now();
  nodes_ = 0;
  if (tt_.size() != kTTSize) tt_.assign(kTTSize, {});
  std::fill(&killers_[0][0], &killers_[0][0] + 128 * 2, Move{});
  std::memset(history_, 0, sizeof(history_));

  std::vector<Move> rootMoves;
  MoveGen::generateLegal(root, rootMoves);
  if (rootMoves.empty()) {
    return {"", root.inCheck(root.sideToMove()) ? -kMateScore : 0, 0};
  }

  pvMove_ = rootMoves.front();
  score_ = 0;
  completedDepth_ = 0;

  constexpr int kAspiration = 50;

  for (int depth = 1; depth <= limits_.maxDepth; ++depth) {
    int alpha = -kInf;
    int beta = kInf;
    if (depth >= 4) {
      alpha = score_ - kAspiration;
      beta = score_ + kAspiration;
    }

    int score = 0;
    int failLow = 0;
    int failHigh = 0;
    while (true) {
      score = negamax(root, depth, alpha, beta, 0, true);
      if (stop_) break;
      if (score <= alpha) {
        ++failLow;
        alpha = (failLow >= 2) ? -kInf : score_ - kAspiration * (1 << failLow);
        continue;
      }
      if (score >= beta) {
        ++failHigh;
        beta = (failHigh >= 2) ? kInf : score_ + kAspiration * (1 << failHigh);
        continue;
      }
      break;
    }

    if (stop_) break;

    score_ = score;
    completedDepth_ = depth;
    if (std::abs(score) > kMateScore - 100) break;
  }

  SearchResult result;
  result.bestMoveUci = moveToUci(pvMove_);
  result.scoreCp = score_;
  result.depthReached = completedDepth_;
  return result;
}

int Searcher::negamax(Position& pos, int depth, int alpha, int beta, int ply,
                      bool allowNull) {
  if (stop_.load() || shouldStop()) {
    stop_ = true;
    return evaluate(pos);
  }

  ++nodes_;
  if (pos.isDraw()) return 0;
  if (ply >= 120) return evaluate(pos);

  const bool inCheck = pos.inCheck(pos.sideToMove());
  if (inCheck) ++depth;

  if (depth <= 0) return quiescence(pos, alpha, beta, ply);

  const Bitboard key = pos.hash();
  Move ttMove{};
  int ttScore = 0;
  if (probeTT(key, depth, alpha, beta, ply, ttScore, ttMove) && ply > 0) {
    return ttScore;
  }

  const int staticEval = evaluate(pos);

  // Null-move pruning.
  if (allowNull && !inCheck && depth >= 3 && beta < kInf - 100 &&
      pos.hasNonPawnMaterial(pos.sideToMove()) && staticEval >= beta) {
    const int R = 2 + depth / 4;
    NullUndo nundo;
    pos.makeNullMove(nundo);
    const int score = -negamax(pos, depth - 1 - R, -beta, -beta + 1, ply + 1, false);
    pos.unmakeNullMove(nundo);
    if (stop_) return evaluate(pos);
    if (score >= beta) return beta;
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

  Undo undo;
  for (const Move& m : moves) {
    const bool quiet = isQuiet(m);

    pos.makeMove(m, undo);
    const bool givesChk = pos.inCheck(pos.sideToMove());

    int score;
    if (moveIndex == 0) {
      score = -negamax(pos, depth - 1, -beta, -alpha, ply + 1, true);
    } else {
      int reduction = 0;
      if (depth >= 3 && moveIndex >= 3 && quiet && !givesChk && !inCheck) {
        reduction = 1 + (moveIndex >= 6) + (depth >= 6);
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

    pos.unmakeMove(m, undo);
    ++moveIndex;

    if (stop_) return evaluate(pos);

    if (score > best) {
      best = score;
      bestMove = m;
    }
    if (score > alpha) {
      alpha = score;
      flag = 0;
      if (ply == 0) pvMove_ = m;
    }
    if (alpha >= beta) {
      flag = 2;
      if (!isCapture(m)) {
        killers_[ply][1] = killers_[ply][0];
        killers_[ply][0] = m;
        history_[static_cast<int>(m.from)][static_cast<int>(m.to)] += depth * depth;
      }
      break;
    }
  }

  storeTT(key, best, depth, flag, bestMove, ply);
  return best;
}

int Searcher::quiescence(Position& pos, int alpha, int beta, int ply) {
  if (stop_.load() || shouldStop()) {
    stop_ = true;
    return evaluate(pos);
  }
  ++nodes_;
  if (ply >= 120) return evaluate(pos);

  const bool inCheck = pos.inCheck(pos.sideToMove());
  if (!inCheck) {
    const int standPat = evaluate(pos);
    if (standPat >= beta) return beta;
    if (alpha < standPat) alpha = standPat;
  }

  std::vector<Move> moves;
  MoveGen::generateLegal(pos, moves);

  if (inCheck) {
    if (moves.empty()) return -kMateScore + ply;
    // While in check, search all legal escapes.
  } else {
    // Captures and promotions only; prune clearly losing captures with SEE.
    moves.erase(std::remove_if(moves.begin(), moves.end(),
                               [&](const Move& m) {
                                 if (!(isCapture(m) || m.promotion != PieceType::None))
                                   return true;
                                 return pos.see(m) < -50;
                               }),
                moves.end());
  }

  orderMoves(pos, moves, {}, ply);

  Undo undo;
  for (const Move& m : moves) {
    pos.makeMove(m, undo);
    const int score = -quiescence(pos, -beta, -alpha, ply + 1);
    pos.unmakeMove(m, undo);
    if (stop_) return evaluate(pos);
    if (score >= beta) return beta;
    if (score > alpha) alpha = score;
  }
  return alpha;
}

void Searcher::orderMoves(Position& pos, std::vector<Move>& moves, const Move& ttMove,
                          int ply) {
  auto scoreMove = [&](const Move& m) {
    if (m.from == ttMove.from && m.to == ttMove.to && m.promotion == ttMove.promotion)
      return 1000000;
    if (isCapture(m) || m.promotion != PieceType::None) {
      return 500000 + pos.see(m) + mvvLva(pos, m);
    }
    if (killers_[ply][0] == m) return 400000;
    if (killers_[ply][1] == m) return 390000;
    return history_[static_cast<int>(m.from)][static_cast<int>(m.to)];
  };

  std::sort(moves.begin(), moves.end(),
            [&](const Move& a, const Move& b) { return scoreMove(a) > scoreMove(b); });
}

SearchResult findBestMove(const std::string& fen, SearchLimits limits) {
  Position pos;
  if (!pos.setFromFen(fen)) {
    return {"", 0, 0};
  }
  Searcher searcher;
  return searcher.search(pos, limits);
}

}  // namespace chess
