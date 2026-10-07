#include "chess/bitboard.hpp"

#include <cmath>
#include <cstdlib>

namespace chess {
namespace bb {

Bitboard lineBB[64][64];
Bitboard betweenBB[64][64];
Bitboard rayBB[64][8];
Bitboard pawnAttacks[2][64];
Bitboard pawnAttackers[2][64];
Bitboard knightAttacks[64];
Bitboard kingAttacks[64];

namespace {

constexpr Bitboard kFileA = 0x0101010101010101ULL;
constexpr Bitboard kFileH = 0x8080808080808080ULL;

constexpr int kKnightDeltas[8] = {17, 15, 10, 6, -6, -10, -15, -17};
constexpr int kKingDeltas[8] = {8, -8, 1, -1, 9, 7, -7, -9};

constexpr int kDirDf[8] = {1, -1, 1, -1, 0, 0, 1, -1};
constexpr int kDirDr[8] = {1, 1, -1, -1, 1, -1, 0, 0};

Bitboard rayFrom(Square sq, int dir) {
  Bitboard ray = 0;
  int f = fileOf(sq);
  int r = rankOf(sq);
  for (;;) {
    f += kDirDf[dir];
    r += kDirDr[dir];
    if (f < 0 || f > 7 || r < 0 || r > 7) break;
    ray |= 1ULL << static_cast<int>(squareFrom(f, r));
  }
  return ray;
}

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

}  // namespace

void init() {
  for (int sq = 0; sq < 64; ++sq) {
    Bitboard k = 0;
    for (int d : kKnightDeltas) {
      const int t = sq + d;
      if (t >= 0 && t < 64 &&
          std::abs(fileOf(static_cast<Square>(sq)) - fileOf(static_cast<Square>(t))) <= 2) {
        k |= 1ULL << t;
      }
    }
    knightAttacks[sq] = k;

    Bitboard kg = 0;
    for (int d : kKingDeltas) {
      const int t = sq + d;
      if (t >= 0 && t < 64 &&
          std::abs(fileOf(static_cast<Square>(sq)) - fileOf(static_cast<Square>(t))) <= 1) {
        kg |= 1ULL << t;
      }
    }
    kingAttacks[sq] = kg;

    Bitboard w = 0;
    Bitboard bl = 0;
    const int f = fileOf(static_cast<Square>(sq));
    const int r = rankOf(static_cast<Square>(sq));
    if (f < 7 && r < 7) w |= 1ULL << static_cast<int>(squareFrom(f + 1, r + 1));
    if (f > 0 && r < 7) w |= 1ULL << static_cast<int>(squareFrom(f - 1, r + 1));
    if (f < 7 && r > 0) bl |= 1ULL << static_cast<int>(squareFrom(f + 1, r - 1));
    if (f > 0 && r > 0) bl |= 1ULL << static_cast<int>(squareFrom(f - 1, r - 1));
    pawnAttacks[0][sq] = w;
    pawnAttacks[1][sq] = bl;
  }

  for (int sq = 0; sq < 64; ++sq) {
    for (int color = 0; color < 2; ++color) {
      Bitboard mask = 0;
      for (int psq = 0; psq < 64; ++psq) {
        if (pawnAttacks[color][psq] & (1ULL << sq)) mask |= 1ULL << psq;
      }
      pawnAttackers[color][sq] = mask;
    }
  }

  for (int sq = 0; sq < 64; ++sq) {
    for (int dir = 0; dir < 8; ++dir) {
      rayBB[sq][dir] = rayFrom(static_cast<Square>(sq), dir);
    }
  }

  for (int a = 0; a < 64; ++a) {
    for (int b = 0; b < 64; ++b) {
      if (a == b) continue;
      Bitboard line = 0;
      for (int dir = 0; dir < 8; ++dir) {
        if (rayBB[a][dir] & (1ULL << b)) {
          line = rayBB[a][dir] | (1ULL << a) | (1ULL << b);
          break;
        }
      }
      lineBB[a][b] = line;
    }
  }

  for (int a = 0; a < 64; ++a) {
    for (int b = 0; b < 64; ++b) {
      if (a == b || lineBB[a][b] == 0) {
        betweenBB[a][b] = 0;
        continue;
      }
      Bitboard bb = lineBB[a][b];
      bb &= ~((1ULL << a) | (1ULL << b));
      if ((1ULL << a) > (1ULL << b)) {
        bb &= ~((1ULL << b) - 1);
        bb &= (1ULL << a) - 1;
      } else {
        bb &= ~((1ULL << a) - 1);
        bb &= (1ULL << b) - 1;
      }
      betweenBB[a][b] = bb;
    }
  }
}

Bitboard slidingAttacks(Square sq, Bitboard occ, bool bishop) {
  Bitboard attacks = 0;
  const int start = bishop ? 0 : 4;
  const int end = bishop ? 4 : 8;
  for (int dir = start; dir < end; ++dir) {
    attacks |= slidingAttacksOne(sq, occ, dir);
  }
  return attacks;
}

Bitboard attackersTo(Square sq, Color byColor, const Bitboard pieces[13],
                     Bitboard occ) {
  const int c = static_cast<int>(byColor);
  Bitboard attackers = pawnAttackers[c][static_cast<int>(sq)] & pieces[c == 0 ? 1 : 7];
  attackers |= knightAttacks[static_cast<int>(sq)] & pieces[c == 0 ? 2 : 8];
  attackers |= kingAttacks[static_cast<int>(sq)] & pieces[c == 0 ? 6 : 12];
  const Bitboard bishops = pieces[c == 0 ? 3 : 9];
  const Bitboard rooks = pieces[c == 0 ? 4 : 10];
  const Bitboard queens = pieces[c == 0 ? 5 : 11];
  attackers |= slidingAttacks(sq, occ, true) & (bishops | queens);
  attackers |= slidingAttacks(sq, occ, false) & (rooks | queens);
  return attackers;
}

}  // namespace bb
}  // namespace chess
