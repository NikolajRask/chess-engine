#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/position.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace chess {
namespace {

TEST(IncrementalEval, StartPositionMatchesFull) {
  Position pos;
  EXPECT_EQ(evaluate(pos), evaluateFull(pos));
  EXPECT_GT(pos.phase(), 0);
}

TEST(IncrementalEval, AfterMovesMatchesFull) {
  Position pos;
  std::vector<Move> moves;
  MoveGen::generateLegal(pos, moves);
  ASSERT_FALSE(moves.empty());

  for (const Move& m : moves) {
    Undo undo;
    pos.makeMove(m, undo);
    EXPECT_EQ(evaluate(pos), evaluateFull(pos)) << "after " << moveToUci(m);
    pos.unmakeMove(m, undo);
    EXPECT_EQ(evaluate(pos), evaluateFull(pos)) << "unmake " << moveToUci(m);
  }
}

TEST(IncrementalEval, KiwipeteAndCaptures) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PP3/2P2P1q/2N2B1P/1P2PBP1/R2QK2R w KQkq - 0 1"));
  EXPECT_EQ(evaluate(pos), evaluateFull(pos));

  std::vector<Move> moves;
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
    EXPECT_EQ(evaluate(pos), evaluateFull(pos)) << moveToUci(m);

    std::vector<Move> replies;
    MoveGen::generateLegal(pos, replies);
    const int limit = std::min(8, static_cast<int>(replies.size()));
    for (int i = 0; i < limit; ++i) {
      Undo u2;
      pos.makeMove(replies[static_cast<size_t>(i)], u2);
      EXPECT_EQ(evaluate(pos), evaluateFull(pos));
      pos.unmakeMove(replies[static_cast<size_t>(i)], u2);
    }
    pos.unmakeMove(m, undo);
  }
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

}  // namespace
}  // namespace chess
