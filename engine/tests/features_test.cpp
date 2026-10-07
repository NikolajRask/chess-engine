#include "chess/book.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"
#include "chess/timeman.hpp"

#include <gtest/gtest.h>

namespace chess {
namespace {

TEST(Book, StartPosHasMove) {
  Position pos;
  auto m = globalBook().probe(pos);
  ASSERT_TRUE(m.has_value());
  std::vector<Move> legal;
  MoveGen::generateLegal(pos, legal);
  bool found = false;
  for (const Move& lm : legal) {
    if (lm == *m) found = true;
  }
  EXPECT_TRUE(found);
}

TEST(Skill, ApplyLimitsClampsDepth) {
  SearchLimits lim;
  lim.skillLevel = 3;
  lim.maxDepth = 64;
  lim.nodes = 0;
  applySkillLimits(lim);
  EXPECT_LE(lim.maxDepth, 4);
  EXPECT_GT(lim.nodes, 0u);
  EXPECT_GE(skillCandidateCount(3), 3);

  lim.skillLevel = 20;
  lim.maxDepth = 64;
  lim.nodes = 0;
  applySkillLimits(lim);
  EXPECT_EQ(lim.maxDepth, 64);
  EXPECT_EQ(lim.nodes, 0u);
  EXPECT_EQ(skillCandidateCount(20), 1);
}

TEST(TimeMan, MovetimeSoftHard) {
  SearchLimits lim;
  lim.movetime = 1000;
  const auto a = allocateTime(lim, Color::White);
  EXPECT_EQ(a.hardMs, 1000);
  EXPECT_EQ(a.softMs, 800);
}

TEST(TimeMan, ClockAllocation) {
  SearchLimits lim;
  lim.wtime = 60000;
  lim.winc = 1000;
  lim.movestogo = 40;
  lim.timeMs = 0;
  const auto a = allocateTime(lim, Color::White);
  EXPECT_GT(a.hardMs, 0);
  EXPECT_LE(a.softMs, a.hardMs);
}

TEST(Search, BookMoveFromFindBest) {
  SearchLimits lim;
  lim.useBook = true;
  lim.timeMs = 50;
  lim.skillLevel = 20;
  const auto r = findBestMove("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", lim);
  EXPECT_TRUE(r.fromBook);
  EXPECT_FALSE(r.bestMoveUci.empty());
}

TEST(Search, LegalMoveFromUci) {
  Position pos;
  auto m = legalMoveFromUci(pos, "e2e4");
  ASSERT_TRUE(m.has_value());
  EXPECT_EQ(moveToUci(*m), "e2e4");
}

}  // namespace
}  // namespace chess
