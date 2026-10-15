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
  MoveList legal;
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

TEST(Search, FindsMateInOne) {
  SearchLimits lim;
  lim.useBook = false;
  lim.maxDepth = 6;
  lim.timeMs = 5000;
  lim.skillLevel = 20;
  // Kc7 + Rb1 vs Ka8: Rb8# or Ra1# (both mate in 1)
  const auto r = findBestMove("k7/2K5/8/8/8/8/8/1R6 w - - 0 1", lim);
  ASSERT_TRUE(r.scoreMate.has_value());
  EXPECT_EQ(*r.scoreMate, 1);
  EXPECT_TRUE(r.bestMoveUci == "b1b8" || r.bestMoveUci == "b1a1");
}

TEST(Search, FindsMateInTwo) {
  SearchLimits lim;
  lim.useBook = false;
  lim.maxDepth = 8;
  lim.timeMs = 5000;
  lim.skillLevel = 20;
  // Kc6 + Rc1 vs Kb8: Rc8+ Ka7 Ra8#
  const auto r = findBestMove("1k6/8/2K5/8/8/8/8/2R5 w - - 0 1", lim);
  ASSERT_TRUE(r.scoreMate.has_value());
  EXPECT_GT(*r.scoreMate, 0);
  EXPECT_LE(*r.scoreMate, 3);
}

TEST(Search, FindsMateInThree) {
  SearchLimits lim;
  lim.useBook = false;
  lim.maxDepth = 10;
  lim.timeMs = 8000;
  lim.skillLevel = 20;
  // Ladder mate: Rd1 vs Kc8 with Kc6 — mate in at most 3 moves (5 plies).
  const auto r = findBestMove("2k5/8/2K5/8/8/8/8/3R4 w - - 0 1", lim);
  ASSERT_TRUE(r.scoreMate.has_value());
  EXPECT_GT(*r.scoreMate, 0);
  EXPECT_LE(*r.scoreMate, 5);
}

TEST(Search, FindsQuietCheckMateInOne) {
  SearchLimits lim;
  lim.useBook = false;
  lim.maxDepth = 4;
  lim.timeMs = 3000;
  lim.skillLevel = 20;
  // Back-rank: Re8# is a quiet check mate (exercises QS checks).
  const auto r = findBestMove("6k1/5ppp/8/8/8/8/5PPP/4R1K1 w - - 0 1", lim);
  ASSERT_TRUE(r.scoreMate.has_value());
  EXPECT_EQ(*r.scoreMate, 1);
  EXPECT_EQ(r.bestMoveUci, "e1e8");
}

}  // namespace
}  // namespace chess
