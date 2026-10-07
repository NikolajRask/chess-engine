#include "chess/movegen.hpp"
#include "chess/position.hpp"

#include <gtest/gtest.h>

namespace chess {
namespace {

TEST(Perft, StartPosition) {
  Position pos;
  EXPECT_EQ(perft(pos, 1), 20);
  EXPECT_EQ(perft(pos, 2), 400);
  EXPECT_EQ(perft(pos, 3), 8902);
  EXPECT_EQ(perft(pos, 4), 197281);
}

TEST(Perft, Kiwipete) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen(
      "r3k2r/p1ppqpb1/bn2pnp1/3PP3/2P2P1q/2N2B1P/1P2PBP1/R2QK2R w KQkq - 0 1"));
  EXPECT_EQ(perft(pos, 1), 42);
  EXPECT_EQ(perft(pos, 2), 1997);
  EXPECT_EQ(perft(pos, 3), 83597);
}

TEST(Perft, Position3) {
  Position pos;
  ASSERT_TRUE(pos.setFromFen(
      "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q2N2Pp/p1P2P2/R2Q1RK1 w kq - 0 1"));
  EXPECT_EQ(perft(pos, 1), 38);
  EXPECT_EQ(perft(pos, 2), 1454);
}

}  // namespace
}  // namespace chess
