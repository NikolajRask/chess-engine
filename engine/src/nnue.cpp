#include "chess/nnue.hpp"

#include "chess/position.hpp"

#include <atomic>
#include <fstream>
#include <mutex>
#include <vector>

namespace chess {
namespace nnue {

namespace {

constexpr uint32_t kMagic = 0x49415343u;  // "CSAI" little-endian
constexpr uint32_t kVersion = 1;

struct Network {
  std::vector<int16_t> ftW;  // [kInputDim * kAccDim] column-major per feature
  std::vector<int16_t> ftB;
  std::vector<int16_t> l1W;  // [kHiddenDim * (2 * kAccDim)]
  std::vector<int16_t> l1B;
  std::vector<int16_t> l2W;
  int16_t l2B = 0;
};

Network gNet;
std::atomic<bool> gReady{false};
std::atomic<bool> gUse{false};
std::mutex gLoadMu;

int featureIndex(Color perspective, Square sq, Piece p) {
  const Color pc = pieceColor(p);
  const PieceType pt = pieceType(p);
  int typeIdx = static_cast<int>(pt) - 1;
  if (pc != perspective) typeIdx += 6;
  int s = static_cast<int>(sq);
  if (perspective == Color::Black) s ^= 63;
  return typeIdx * 64 + s;
}

void addFeature(std::array<int16_t, kAccDim>& acc, int feat) {
  const int16_t* col = gNet.ftW.data() + feat * kAccDim;
  for (int i = 0; i < kAccDim; ++i) acc[static_cast<size_t>(i)] += col[i];
}

void subFeature(std::array<int16_t, kAccDim>& acc, int feat) {
  const int16_t* col = gNet.ftW.data() + feat * kAccDim;
  for (int i = 0; i < kAccDim; ++i) acc[static_cast<size_t>(i)] -= col[i];
}

int16_t crelu(int16_t x) {
  if (x < 0) return 0;
  if (x > kCreluMax) return static_cast<int16_t>(kCreluMax);
  return x;
}

}  // namespace

bool loaded() { return gReady.load(std::memory_order_acquire); }

bool useNnue() {
  return gUse.load(std::memory_order_relaxed) && gReady.load(std::memory_order_acquire);
}

void setUseNnue(bool on) { gUse.store(on, std::memory_order_relaxed); }

void unload() {
  std::lock_guard lock(gLoadMu);
  gReady.store(false, std::memory_order_release);
  gNet = Network{};
}

bool load(std::string_view path) {
  std::ifstream in(std::string(path), std::ios::binary);
  if (!in) return false;

  uint32_t magic = 0, version = 0, acc = 0, hidden = 0;
  in.read(reinterpret_cast<char*>(&magic), 4);
  in.read(reinterpret_cast<char*>(&version), 4);
  in.read(reinterpret_cast<char*>(&acc), 4);
  in.read(reinterpret_cast<char*>(&hidden), 4);
  if (!in || magic != kMagic || version != kVersion || acc != static_cast<uint32_t>(kAccDim) ||
      hidden != static_cast<uint32_t>(kHiddenDim)) {
    return false;
  }

  Network net;
  net.ftW.resize(static_cast<size_t>(kInputDim) * kAccDim);
  net.ftB.resize(kAccDim);
  net.l1W.resize(static_cast<size_t>(kHiddenDim) * 2 * kAccDim);
  net.l1B.resize(kHiddenDim);
  net.l2W.resize(kHiddenDim);

  const auto readVec = [&](std::vector<int16_t>& v) {
    in.read(reinterpret_cast<char*>(v.data()),
            static_cast<std::streamsize>(v.size() * sizeof(int16_t)));
  };
  readVec(net.ftW);
  readVec(net.ftB);
  readVec(net.l1W);
  readVec(net.l1B);
  readVec(net.l2W);
  in.read(reinterpret_cast<char*>(&net.l2B), sizeof(int16_t));
  if (!in) return false;

  std::lock_guard lock(gLoadMu);
  gReady.store(false, std::memory_order_release);
  gNet = std::move(net);
  gReady.store(true, std::memory_order_release);
  return true;
}

bool loadDefault() {
  static const char* kPaths[] = {"nets/default.nnue", "../nets/default.nnue",
                                 "../../nets/default.nnue", "./default.nnue"};
  for (const char* p : kPaths) {
    if (load(p)) return true;
  }
  return false;
}

void refresh(const Position& pos, Accumulator& acc) {
  if (!loaded()) {
    acc = Accumulator{};
    return;
  }
  for (int i = 0; i < kAccDim; ++i) {
    acc.white[static_cast<size_t>(i)] = gNet.ftB[static_cast<size_t>(i)];
    acc.black[static_cast<size_t>(i)] = gNet.ftB[static_cast<size_t>(i)];
  }
  for (int sq = 0; sq < 64; ++sq) {
    const Piece p = pos.pieceOn(static_cast<Square>(sq));
    if (p == Piece::None) continue;
    addFeature(acc.white, featureIndex(Color::White, static_cast<Square>(sq), p));
    addFeature(acc.black, featureIndex(Color::Black, static_cast<Square>(sq), p));
  }
}

void addPiece(Accumulator& acc, Square sq, Piece p) {
  if (!loaded() || p == Piece::None || sq == Square::None) return;
  addFeature(acc.white, featureIndex(Color::White, sq, p));
  addFeature(acc.black, featureIndex(Color::Black, sq, p));
}

void removePiece(Accumulator& acc, Square sq, Piece p) {
  if (!loaded() || p == Piece::None || sq == Square::None) return;
  subFeature(acc.white, featureIndex(Color::White, sq, p));
  subFeature(acc.black, featureIndex(Color::Black, sq, p));
}

int evaluate(const Position& pos, const Accumulator& acc) {
  if (!useNnue()) return 0;

  const bool stmWhite = pos.sideToMove() == Color::White;
  const auto& stm = stmWhite ? acc.white : acc.black;
  const auto& nstm = stmWhite ? acc.black : acc.white;

  int16_t input[2 * kAccDim];
  for (int i = 0; i < kAccDim; ++i) {
    input[i] = crelu(stm[static_cast<size_t>(i)]);
    input[kAccDim + i] = crelu(nstm[static_cast<size_t>(i)]);
  }

  // Float train: x=crelu(FT)/QA in [0,1], h=relu(W1 x+b1), y=W2 h+b2.
  // Int: input=crelu(acc); sum=(W1 x+b1)*QA; hidden=max(0,sum) (=h*QA);
  // out = W2·hidden + b2*QA; score = out/QA.
  int32_t hidden[kHiddenDim];
  for (int i = 0; i < kHiddenDim; ++i) {
    int32_t sum = gNet.l1B[static_cast<size_t>(i)];
    const int16_t* row = gNet.l1W.data() + i * (2 * kAccDim);
    for (int j = 0; j < 2 * kAccDim; ++j) {
      sum += static_cast<int32_t>(row[j]) * static_cast<int32_t>(input[j]);
    }
    if (sum < 0) sum = 0;
    // Soft cap (~relu up to 512) to keep L2 in int32 range.
    if (sum > kCreluMax * 512) sum = kCreluMax * 512;
    hidden[i] = sum;
  }

  int32_t out = gNet.l2B;
  for (int i = 0; i < kHiddenDim; ++i) {
    out += static_cast<int32_t>(gNet.l2W[static_cast<size_t>(i)]) * hidden[i];
  }
  return static_cast<int>(out / kOutputScale);
}

}  // namespace nnue
}  // namespace chess
