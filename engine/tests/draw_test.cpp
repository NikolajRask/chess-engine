#include "chess/game.hpp"
#include "chess/movegen.hpp"
#include "chess/position.hpp"
#include "chess/search.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace chess {
namespace {

TEST(DrawRules, FiftyAndSeventyFiveMove) {
  Position pos;
  // Keep a rook so this is not an insufficient-material draw.
  ASSERT_TRUE(pos.setFromFen("4k3/8/8/8/8/8/8/4K2R w - - 99 50"));
  EXPECT_FALSE(pos.isFiftyMoveDraw());
  EXPECT_FALSE(pos.isDraw());

  ASSERT_TRUE(pos.setFromFen("4k3/8/8/8/8/8/8/4K2R w - - 100 50"));
  EXPECT_TRUE(pos.isFiftyMoveDraw());
  EXPECT_TRUE(pos.isDraw());
  EXPECT_FALSE(pos.isSeventyFiveMoveDraw());
  {
    std::vector<Bitboard> hist{pos.hash()};
    EXPECT_EQ(gameOutcome(pos, hist), GameOutcome::DrawFiftyMove);
  }

  ASSERT_TRUE(pos.setFromFen("4k3/8/8/8/8/8/8/4K2R w - - 150 80"));
  EXPECT_TRUE(pos.isSeventyFiveMoveDraw());
  EXPECT_TRUE(pos.isDraw());

  std::vector<Bitboard> hist{pos.hash()};
  EXPECT_EQ(gameOutcome(pos, hist), GameOutcome::DrawSeventyFiveMove);
}

TEST(DrawRules, StalemateAndCheckmate) {
  Position pos;
  // Black to move, stalemate
  ASSERT_TRUE(pos.setFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
  std::vector<Bitboard> hist{pos.hash()};
  EXPECT_EQ(gameOutcome(pos, hist), GameOutcome::Stalemate);

  // Black to move, checkmate
  ASSERT_TRUE(pos.setFromFen("7k/5QQ1/6K1/8/8/8/8/8 b - - 0 1"));
  hist = {pos.hash()};
  EXPECT_EQ(gameOutcome(pos, hist), GameOutcome::Checkmate);
}

TEST(DrawRules, ThreefoldRepetition) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"));

  // Knights shuttle: Nb1-c3-b1 / Nb8-c6-b8, repeated.
  const std::vector<std::string> moves = {
      "b1c3", "b8c6", "c3b1", "c6b8",  // back to start (2nd)
      "b1c3", "b8c6", "c3b1", "c6b8",  // back to start (3rd) → threefold
  };
  const std::vector<Bitboard> hist = applyUciMoves(pos, moves);
  EXPECT_EQ(hist.size(), moves.size() + 1);
  EXPECT_TRUE(isThreefoldRepetition(pos.hash(), hist, pos.halfmoveClock()));
  EXPECT_EQ(gameOutcome(pos, hist), GameOutcome::DrawThreefold);
}

TEST(DrawRules, SearchReportsTerminalDraw) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen("8/8/8/4k3/8/4K3/8/8 w - - 0 1"));
  SearchLimits lim;
  lim.useBook = false;
  lim.timeMs = 50;
  lim.maxDepth = 4;
  const SearchResult r = globalSearcher().search(pos, lim, {pos.hash()});
  EXPECT_TRUE(r.gameOver);
  EXPECT_EQ(r.outcome, "insufficient_material");
  EXPECT_TRUE(r.bestMoveUci.empty());
  EXPECT_EQ(r.scoreCp, 0);
}

TEST(DrawRules, SearchReportsStalemate) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1"));
  SearchLimits lim;
  lim.useBook = false;
  lim.timeMs = 50;
  lim.maxDepth = 4;
  const SearchResult r = globalSearcher().search(pos, lim, {pos.hash()});
  EXPECT_TRUE(r.gameOver);
  EXPECT_EQ(r.outcome, "stalemate");
  EXPECT_TRUE(r.bestMoveUci.empty());
  EXPECT_EQ(r.scoreCp, 0);
}

// White is up a rook, but Black has a forced Qg3+/Qh3+ perpetual.
// Depth 1 drops into QS mid-cycle; quiet checks must continue deep enough
// for the 2-fold to score 0 (otherwise stand-pat leaks a non-draw eval).
TEST(DrawRules, SearchSeesForcedPerpetual) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen("6k1/5ppp/8/8/8/7q/8/Q4RK1 b - - 0 1"));
  SearchLimits lim;
  lim.useBook = false;
  lim.timeMs = 2000;
  lim.maxDepth = 1;
  lim.skillLevel = 20;
  const SearchResult r = globalSearcher().search(pos, lim, {pos.hash()});
  EXPECT_FALSE(r.gameOver);
  EXPECT_NEAR(r.scoreCp, 0, 50);
}

}  // namespace
}  // namespace chess
