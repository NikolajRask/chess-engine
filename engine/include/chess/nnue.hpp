#pragma once

#include "chess/types.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace chess {

class Position;

namespace nnue {

inline constexpr int kInputDim = 768;   // 12 pieces × 64 squares
inline constexpr int kAccDim = 256;
inline constexpr int kHiddenDim = 32;
inline constexpr int kCreluMax = 127;      // QA: FT / crelu quantization
inline constexpr int kOutputScale = 8128;  // QA * QB (127 * 64) dequant to cp

struct Accumulator {
  std::array<int16_t, kAccDim> white{};
  std::array<int16_t, kAccDim> black{};
};

bool load(std::string_view path);
void unload();
bool loaded();
bool useNnue();
void setUseNnue(bool on);

// Default search path: nets/default.nnue relative to cwd, then beside binary hints.
bool loadDefault();

void refresh(const Position& pos, Accumulator& acc);
void addPiece(Accumulator& acc, Square sq, Piece p);
void removePiece(Accumulator& acc, Square sq, Piece p);

// Side-to-move relative score in centipawns.
int evaluate(const Position& pos, const Accumulator& acc);

// Global toggle + network; positions keep their own Accumulator.
}  // namespace nnue
}  // namespace chess
