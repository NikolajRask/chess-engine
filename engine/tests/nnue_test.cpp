#include "chess/eval.hpp"
#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/position.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace chess {
namespace {

std::string writeTempNet() {
  // Minimal valid net: zeros except FT bias and a tiny output bias so eval ≠ 0.
  const char* path = "nnue_test_temp.nnue";
  std::ofstream out(path, std::ios::binary);
  const uint32_t magic = 0x49415343u;
  const uint32_t version = 1;
  const uint32_t acc = static_cast<uint32_t>(nnue::kAccDim);
  const uint32_t hidden = static_cast<uint32_t>(nnue::kHiddenDim);
  out.write(reinterpret_cast<const char*>(&magic), 4);
  out.write(reinterpret_cast<const char*>(&version), 4);
  out.write(reinterpret_cast<const char*>(&acc), 4);
  out.write(reinterpret_cast<const char*>(&hidden), 4);

  std::vector<int16_t> ftW(static_cast<size_t>(nnue::kInputDim) * nnue::kAccDim, 0);
  for (int f = 0; f < nnue::kInputDim; ++f) {
    ftW[static_cast<size_t>(f) * nnue::kAccDim + 0] = 1;
  }
  std::vector<int16_t> ftB(nnue::kAccDim, 0);
  ftB[0] = 10;
  std::vector<int16_t> l1W(static_cast<size_t>(nnue::kHiddenDim) * 2 * nnue::kAccDim, 0);
  l1W[0] = 1;
  std::vector<int16_t> l1B(nnue::kHiddenDim, 0);
  std::vector<int16_t> l2W(nnue::kHiddenDim, 0);
  l2W[0] = 1;
  int16_t l2B = 0;

  out.write(reinterpret_cast<const char*>(ftW.data()),
            static_cast<std::streamsize>(ftW.size() * 2));
  out.write(reinterpret_cast<const char*>(ftB.data()),
            static_cast<std::streamsize>(ftB.size() * 2));
  out.write(reinterpret_cast<const char*>(l1W.data()),
            static_cast<std::streamsize>(l1W.size() * 2));
  out.write(reinterpret_cast<const char*>(l1B.data()),
            static_cast<std::streamsize>(l1B.size() * 2));
  out.write(reinterpret_cast<const char*>(l2W.data()),
            static_cast<std::streamsize>(l2W.size() * 2));
  out.write(reinterpret_cast<const char*>(&l2B), 2);
  out.close();
  return path;
}

TEST(Nnue, LoadAndIncrementalMatchesRefresh) {
  const std::string path = writeTempNet();
  ASSERT_TRUE(nnue::load(path));
  nnue::setUseNnue(true);

  Position pos;
  ASSERT_TRUE(pos.setFromFen(
      "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"));
  nnue::Accumulator refreshed;
  nnue::refresh(pos, refreshed);
  EXPECT_EQ(pos.nnueAcc().white, refreshed.white);
  EXPECT_EQ(pos.nnueAcc().black, refreshed.black);

  MoveList moves;
  MoveGen::generateLegal(pos, moves);
  ASSERT_FALSE(moves.empty());
  for (int i = 0; i < std::min(12, moves.count()); ++i) {
    Undo undo;
    pos.makeMove(moves[i], undo);
    nnue::Accumulator full;
    nnue::refresh(pos, full);
    EXPECT_EQ(pos.nnueAcc().white, full.white) << moveToUci(moves[i]);
    EXPECT_EQ(pos.nnueAcc().black, full.black) << moveToUci(moves[i]);
    const int score = evaluate(pos);
    EXPECT_EQ(score, nnue::evaluate(pos, full));
    pos.unmakeMove(moves[i], undo);
    nnue::refresh(pos, full);
    EXPECT_EQ(pos.nnueAcc().white, full.white);
    EXPECT_EQ(pos.nnueAcc().black, full.black);
  }

  nnue::setUseNnue(false);
  nnue::unload();
  std::remove(path.c_str());
}

TEST(Nnue, ClassicalFallbackWhenDisabled) {
  const std::string path = writeTempNet();
  ASSERT_TRUE(nnue::load(path));
  nnue::setUseNnue(false);
  Position pos;
  EXPECT_EQ(evaluate(pos), evaluateClassical(pos));
  nnue::unload();
  std::remove(path.c_str());
}

}  // namespace
}  // namespace chess
