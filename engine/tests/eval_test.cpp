#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/pawnhash.hpp"
#include "chess/position.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace chess {
namespace {

TEST(IncrementalEval, StartPositionMatchesFull) {
  nnue::setUseNnue(false);
  Position pos;
  EXPECT_EQ(evaluateClassical(pos), evaluateFull(pos));
  EXPECT_GT(pos.phase(), 0);
}

TEST(IncrementalEval, AfterMovesMatchesFull) {
  nnue::setUseNnue(false);
  Position pos;
  MoveList moves;
  MoveGen::generateLegal(pos, moves);
  ASSERT_FALSE(moves.empty());

  for (const Move& m : moves) {
    Undo undo;
    pos.makeMove(m, undo);
    EXPECT_EQ(evaluateClassical(pos), evaluateFull(pos)) << "after " << moveToUci(m);
    pos.unmakeMove(m, undo);
    EXPECT_EQ(evaluateClassical(pos), evaluateFull(pos)) << "unmake " << moveToUci(m);
  }
}

TEST(IncrementalEval, KiwipeteAndCaptures) {
  nnue::setUseNnue(false);
  Position pos;
  ASSERT_TRUE(pos.setFromFen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PP3/2P2P1q/2N2B1P/1P2PBP1/R2QK2R w KQkq - 0 1"));
  EXPECT_EQ(evaluateClassical(pos), evaluateFull(pos));

  MoveList moves;
  MoveGen::generateLegal(pos, moves);
  for (const Move& m : moves) {
    Undo undo;
    pos.makeMove(m, undo);
    {
      int mg = 0, eg = 0, phase = 0;
      computeMaterial(pos, mg, eg, phase);
      EXPECT_EQ(pos.scoreMg(), mg);
      EXPECT_EQ(pos.scoreEg(), eg);
      EXPECT_EQ(pos.phase(), phase);
    }
    EXPECT_EQ(evaluateClassical(pos), evaluateFull(pos)) << moveToUci(m);

    MoveList replies;
    MoveGen::generateLegal(pos, replies);
    const int limit = std::min(8, replies.count());
    for (int i = 0; i < limit; ++i) {
      Undo u2;
      pos.makeMove(replies[i], u2);
      EXPECT_EQ(evaluateClassical(pos), evaluateFull(pos));
      pos.unmakeMove(replies[i], u2);
    }
    pos.unmakeMove(m, undo);
  }
}

TEST(PawnHash, MatchesDisabledCache) {
  nnue::setUseNnue(false);
  Position pos;
  ASSERT_TRUE(pos.setFromFen(
      "rnbqkb1r/pp3ppp/2p1pn2/3p4/2PP4/2N2N2/PP2PPPP/R1BQKB1R w KQkq - 0 1"));
  setPawnHashEnabled(true);
  threadPawnHash().clear();
  const int withCache = evaluateClassical(pos);
  setPawnHashEnabled(false);
  const int withoutCache = evaluateClassical(pos);
  setPawnHashEnabled(true);
  EXPECT_EQ(withCache, withoutCache);

  // Second eval should hit the cache and still match.
  const int again = evaluateClassical(pos);
  EXPECT_EQ(again, withoutCache);
  EXPECT_GT(threadPawnHash().hits(), 0u);
}

TEST(IncrementalEval, RefreshEval) {
  Position pos;
  const int mg = pos.scoreMg();
  const int eg = pos.scoreEg();
  const int phase = pos.phase();
  pos.refreshEval();
  EXPECT_EQ(pos.scoreMg(), mg);
  EXPECT_EQ(pos.scoreEg(), eg);
  EXPECT_EQ(pos.phase(), phase);
}

TEST(EndgameDraw, InsufficientMaterial) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen("8/8/8/4k3/8/4K3/8/8 w - - 0 1"));  // K vs K
  EXPECT_TRUE(pos.isDraw());

  ASSERT_TRUE(pos.setFromFen("8/8/8/4k3/8/4K3/8/4B3 w - - 0 1"));  // KB vs K
  EXPECT_TRUE(pos.isDraw());

  ASSERT_TRUE(pos.setFromFen("8/8/8/4k3/8/4K3/8/4N3 w - - 0 1"));  // KN vs K
  EXPECT_TRUE(pos.isDraw());

  // Same-color bishops (c1 and a3 share square color)
  ASSERT_TRUE(pos.setFromFen("8/8/8/3k4/8/b2K4/8/2B5 w - - 0 1"));
  EXPECT_TRUE(pos.isDraw());

  // Opposite-color bishops are not a hard draw
  ASSERT_TRUE(pos.setFromFen("8/8/8/3k4/8/3K4/8/2B2b2 w - - 0 1"));
  EXPECT_FALSE(pos.isDraw());

  ASSERT_TRUE(pos.setFromFen("8/8/8/4k3/8/4K3/8/3NN3 w - - 0 1"));  // KNN vs K
  EXPECT_FALSE(pos.isDraw());

  ASSERT_TRUE(pos.setFromFen("8/8/8/4k3/8/4K3/8/3Bn3 w - - 0 1"));  // KB vs KN
  EXPECT_FALSE(pos.isDraw());
}

TEST(EndgameScale, OppositeBishopsAndThinMinors) {
  nnue::setUseNnue(false);
  Position pos;

  // Opposite bishops + equal pawns → scale 64
  ASSERT_TRUE(pos.setFromFen("8/2p1k3/8/8/8/8/2P1K3/2B2b2 w - - 0 1"));
  EXPECT_EQ(endgameScaleFactor(pos), 64);

  // Opposite bishops with 2-pawn edge → scale 96
  ASSERT_TRUE(pos.setFromFen("8/4k3/8/8/8/8/2PPK3/2B2b2 w - - 0 1"));
  EXPECT_EQ(endgameScaleFactor(pos), 96);

  // KB vs KN, no pawns → scale 32
  ASSERT_TRUE(pos.setFromFen("8/8/8/4k3/8/4K3/8/3Bn3 w - - 0 1"));
  EXPECT_EQ(endgameScaleFactor(pos), 32);

  // Starting position → full scale
  Position start;
  EXPECT_EQ(endgameScaleFactor(start), 128);

  // Scaled opposite-bishop eval should be smaller in magnitude than unscaled material edge
  ASSERT_TRUE(pos.setFromFen("8/2p1k3/8/8/8/8/2PPK3/2B2b2 w - - 0 1"));
  EXPECT_LT(endgameScaleFactor(pos), 128);
  EXPECT_LT(std::abs(evaluateClassical(pos)), 250);
}

}  // namespace
}  // namespace chess
