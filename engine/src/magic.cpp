#include "chess/magic.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace chess {
namespace magic {

namespace {

struct Magic {
  Bitboard* attacks = nullptr;
  Bitboard mask = 0;
  Bitboard magic = 0;
  int shift = 0;
};

Magic bishopMagics[64];
Magic rookMagics[64];

// Fancy tables: bishops ~1480 entries total, rooks ~102400; allocate generously.
Bitboard bishopAttackTable[0x1480];
Bitboard rookAttackTable[0x19000];

constexpr int kDirDf[8] = {1, -1, 1, -1, 0, 0, 1, -1};
constexpr int kDirDr[8] = {1, 1, -1, -1, 1, -1, 0, 0};

Bitboard slidingAttacksOne(Square sq, Bitboard occ, int dir) {
  Bitboard attacks = 0;
  int f = fileOf(sq);
  int r = rankOf(sq);
  for (;;) {
    f += kDirDf[dir];
    r += kDirDr[dir];
    if (f < 0 || f > 7 || r < 0 || r > 7) break;
    const Square to = squareFrom(f, r);
    attacks |= 1ULL << static_cast<int>(to);
    if (occ & (1ULL << static_cast<int>(to))) break;
  }
  return attacks;
}

Bitboard rayAttacks(Square sq, Bitboard occ, bool bishop) {
  Bitboard attacks = 0;
  const int start = bishop ? 0 : 4;
  const int end = bishop ? 4 : 8;
  for (int dir = start; dir < end; ++dir) {
    attacks |= slidingAttacksOne(sq, occ, dir);
  }
  return attacks;
}

Bitboard sliderMask(Square sq, bool bishop) {
  // Relevant occupancy: all squares on the ray except the board-edge
  // square (blockers on the rim do not change the attack set index).
  Bitboard mask = 0;
  const int f0 = fileOf(sq);
  const int r0 = rankOf(sq);
  const int start = bishop ? 0 : 4;
  const int end = bishop ? 4 : 8;
  for (int dir = start; dir < end; ++dir) {
    const int df = kDirDf[dir];
    const int dr = kDirDr[dir];
    int f = f0 + df;
    int r = r0 + dr;
    while (f >= 0 && f <= 7 && r >= 0 && r <= 7) {
      const int nf = f + df;
      const int nr = r + dr;
      if (nf < 0 || nf > 7 || nr < 0 || nr > 7) break;  // omit edge square
      mask |= 1ULL << static_cast<int>(squareFrom(f, r));
      f = nf;
      r = nr;
    }
  }
  return mask;
}

int popcount64(Bitboard b) { return __builtin_popcountll(b); }

Bitboard indexToOccupancy(int index, int bits, Bitboard mask) {
  Bitboard occ = 0;
  for (int i = 0; i < bits; ++i) {
    const Bitboard bit = mask & -mask;
    mask ^= bit;
    if (index & (1 << i)) occ |= bit;
  }
  return occ;
}

struct PRNG {
  uint64_t s;
  explicit PRNG(uint64_t seed) : s(seed) {}
  uint64_t next() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ULL;
  }
  Bitboard sparse() { return next() & next() & next(); }
};

bool tryMagic(Square sq, bool bishop, Bitboard magic, Bitboard* table, int tableSize,
              Magic& out) {
  const Bitboard mask = sliderMask(sq, bishop);
  const int bits = popcount64(mask);
  const int shift = 64 - bits;
  const int count = 1 << bits;

  if (count > tableSize) return false;

  Bitboard used[4096];
  std::memset(used, 0, sizeof(used[0]) * static_cast<size_t>(count));

  for (int i = 0; i < count; ++i) {
    const Bitboard occ = indexToOccupancy(i, bits, mask);
    const int idx = static_cast<int>((occ * magic) >> shift);
    if (idx < 0 || idx >= count) return false;
    const Bitboard attacks = rayAttacks(sq, occ, bishop);
    if (used[idx] == 0) {
      used[idx] = attacks;
      table[idx] = attacks;
    } else if (used[idx] != attacks) {
      return false;
    }
  }

  out.attacks = table;
  out.mask = mask;
  out.magic = magic;
  out.shift = shift;
  return true;
}

int findMagics(bool bishop, Bitboard* attackStorage, int storageCap, Magic magics[64]) {
  int offset = 0;
  PRNG rng(bishop ? 0x9E3779B97F4A7C15ULL : 0xBF58476D1CE4E5B9ULL);

  for (int sq = 0; sq < 64; ++sq) {
    const Bitboard mask = sliderMask(static_cast<Square>(sq), bishop);
    const int bits = popcount64(mask);
    const int size = 1 << bits;
    if (offset + size > storageCap) {
      std::fprintf(stderr, "magic table overflow (bishop=%d)\n", bishop ? 1 : 0);
      std::abort();
    }

    Bitboard* table = attackStorage + offset;
    bool ok = false;
    for (int attempt = 0; attempt < 100000000; ++attempt) {
      const Bitboard candidate = rng.sparse();
      if (tryMagic(static_cast<Square>(sq), bishop, candidate, table, size,
                   magics[sq])) {
        ok = true;
        break;
      }
    }
    if (!ok) {
      std::fprintf(stderr, "failed to find magic sq=%d bishop=%d\n", sq, bishop ? 1 : 0);
      std::abort();
    }
    offset += size;
  }
  return offset;
}

}  // namespace

void init() {
  findMagics(true, bishopAttackTable, static_cast<int>(sizeof(bishopAttackTable) / sizeof(Bitboard)),
             bishopMagics);
  findMagics(false, rookAttackTable, static_cast<int>(sizeof(rookAttackTable) / sizeof(Bitboard)),
             rookMagics);
}

Bitboard bishopAttacks(Square sq, Bitboard occ) {
  const Magic& m = bishopMagics[static_cast<int>(sq)];
  occ &= m.mask;
  return m.attacks[(occ * m.magic) >> m.shift];
}

Bitboard rookAttacks(Square sq, Bitboard occ) {
  const Magic& m = rookMagics[static_cast<int>(sq)];
  occ &= m.mask;
  return m.attacks[(occ * m.magic) >> m.shift];
}

}  // namespace magic
}  // namespace chess
