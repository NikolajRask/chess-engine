#include "chess/eval.hpp"

#include "chess/bitboard.hpp"

#include <algorithm>

namespace chess {

namespace {

constexpr int kPieceValue[] = {0, 100, 320, 330, 500, 900, 20000};
constexpr int kPhasePiece[] = {0, 0, 1, 1, 2, 4, 0};  // N B R Q

constexpr int kPstPawn[64] = {
    0,   0,   0,   0,   0,   0,   0,  0,
    5,  10,  10, -20, -20,  10,  10,  5,
    5,  -5, -10,   0,   0, -10,  -5,  5,
    0,   0,   0,  20,  20,   0,   0,  0,
    5,   5,  10,  25,  25,  10,   5,  5,
   10,  10,  20,  30,  30,  20,  10, 10,
   50,  50,  50,  50,  50,  50,  50, 50,
    0,   0,   0,   0,   0,   0,   0,  0,
};

constexpr int kPstKnight[64] = {
  -50, -40, -30, -30, -30, -30, -40, -50,
  -40, -20,   0,   0,   0,   0, -20, -40,
  -30,   0,  10,  15,  15,  10,   0, -30,
  -30,   5,  15,  20,  20,  15,   5, -30,
  -30,   0,  15,  20,  20,  15,   0, -30,
  -30,   5,  10,  15,  15,  10,   5, -30,
  -40, -20,   0,   5,   5,   0, -20, -40,
  -50, -40, -30, -30, -30, -30, -40, -50,
};

constexpr int kPstBishop[64] = {
  -20, -10, -10, -10, -10, -10, -10, -20,
  -10,   0,   0,   0,   0,   0,   0, -10,
  -10,   0,  10,  10,  10,  10,   0, -10,
  -10,   5,   5,  10,  10,   5,   5, -10,
  -10,   0,   5,  10,  10,   5,   0, -10,
  -10,  10,  10,  10,  10,  10,  10, -10,
  -10,   5,   0,   0,   0,   0,   5, -10,
  -20, -10, -10, -10, -10, -10, -10, -20,
};

constexpr int kPstRook[64] = {
    0,   0,   0,   0,   0,   0,  0,   0,
    5,  10,  10,  10,  10,  10,  10,  5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
    0,   0,   0,   5,   5,   0,  0,   0,
};

constexpr int kPstQueen[64] = {
  -20, -10, -10,  -5,  -5, -10, -10, -20,
  -10,   0,   0,   0,   0,   0,   0, -10,
  -10,   0,   5,   5,   5,   5,   0, -10,
   -5,   0,   5,   5,   5,   5,   0,  -5,
    0,   0,   5,   5,   5,   5,   0,  -5,
  -10,   5,   5,   5,   5,   5,   0, -10,
  -10,   0,   5,   0,   0,   0,   0, -10,
  -20, -10, -10,  -5,  -5, -10, -10, -20,
};

constexpr int kPstKingMid[64] = {
  -30, -40, -40, -50, -50, -40, -40, -30,
  -30, -40, -40, -50, -50, -40, -40, -30,
  -30, -40, -40, -50, -50, -40, -40, -30,
  -30, -40, -40, -50, -50, -40, -40, -30,
  -20, -30, -30, -40, -40, -30, -30, -20,
  -10, -20, -20, -20, -20, -20, -20, -10,
   20,  20,   0,   0,   0,   0,  20,  20,
   20,  30,  10,   0,   0,  10,  30,  20,
};

constexpr int kPstKingEnd[64] = {
  -50, -30, -30, -30, -30, -30, -30, -50,
  -30, -10,   0,   0,   0,   0, -10, -30,
  -30,   0,  20,  30,  30,  20,   0, -30,
  -30,   0,  30,  40,  40,  30,   0, -30,
  -30,   0,  30,  40,  40,  30,   0, -30,
  -30,   0,  20,  30,  30,  20,   0, -30,
  -30, -10,   0,   0,   0,   0, -10, -30,
  -50, -30, -30, -30, -30, -30, -30, -50,
};

constexpr int kPassedBonus[8] = {0, 10, 20, 35, 55, 80, 120, 0};

int mirror(int sq, Color c) { return c == Color::White ? sq : 63 - sq; }

int pstMid(PieceType pt, int idx) {
  switch (pt) {
    case PieceType::Pawn:
      return kPstPawn[idx];
    case PieceType::Knight:
      return kPstKnight[idx];
    case PieceType::Bishop:
      return kPstBishop[idx];
    case PieceType::Rook:
      return kPstRook[idx];
    case PieceType::Queen:
      return kPstQueen[idx];
    case PieceType::King:
      return kPstKingMid[idx];
    default:
      return 0;
  }
}

int pstEnd(PieceType pt, int idx) {
  if (pt == PieceType::King) return kPstKingEnd[idx];
  return pstMid(pt, idx);
}

int taper(int mid, int end, int phase) {
  return (mid * phase + end * (24 - phase)) / 24;
}

int evalColor(const Position& pos, Color us, int phase) {
  const Color them = opposite(us);
  const Bitboard occ = pos.occ();
  int mid = 0;
  int end = 0;

  const Bitboard ourPawns = pos.pieces(makePiece(us, PieceType::Pawn));
  const Bitboard theirPawns = pos.pieces(makePiece(them, PieceType::Pawn));
  const Bitboard friendly =
      pos.pieces(makePiece(us, PieceType::Pawn)) |
      pos.pieces(makePiece(us, PieceType::Knight)) |
      pos.pieces(makePiece(us, PieceType::Bishop)) |
      pos.pieces(makePiece(us, PieceType::Rook)) |
      pos.pieces(makePiece(us, PieceType::Queen)) |
      pos.pieces(makePiece(us, PieceType::King));

  for (int sq = 0; sq < 64; ++sq) {
    const Piece p = pos.pieceOn(static_cast<Square>(sq));
    if (p == Piece::None || pieceColor(p) != us) continue;
    const PieceType pt = pieceType(p);
    const int idx = mirror(sq, us);
    const int mat = kPieceValue[static_cast<int>(pt)];
    mid += mat + pstMid(pt, idx);
    end += mat + pstEnd(pt, idx);
  }

  if (bb::popcount(pos.pieces(makePiece(us, PieceType::Bishop))) >= 2) {
    mid += 35;
    end += 50;
  }

  Bitboard knights = pos.pieces(makePiece(us, PieceType::Knight));
  while (knights) {
    const Square from = bb::lsbSquare(knights);
    knights &= knights - 1;
    const int mob = bb::popcount(bb::knightAttacks[static_cast<int>(from)] & ~friendly);
    mid += mob * 4;
    end += mob * 3;
  }

  Bitboard bishops = pos.pieces(makePiece(us, PieceType::Bishop));
  while (bishops) {
    const Square from = bb::lsbSquare(bishops);
    bishops &= bishops - 1;
    const int mob = bb::popcount(bb::slidingAttacks(from, occ, true) & ~friendly);
    mid += mob * 3;
    end += mob * 3;
  }

  Bitboard rooks = pos.pieces(makePiece(us, PieceType::Rook));
  while (rooks) {
    const Square from = bb::lsbSquare(rooks);
    rooks &= rooks - 1;
    const int mob = bb::popcount(bb::slidingAttacks(from, occ, false) & ~friendly);
    mid += mob * 2;
    end += mob * 3;
    const int f = fileOf(from);
    const Bitboard fileMask = 0x0101010101010101ULL << f;
    if (!(ourPawns & fileMask)) {
      mid += (theirPawns & fileMask) ? 12 : 20;
      end += (theirPawns & fileMask) ? 8 : 15;
    }
    if (rankOf(from) == (us == Color::White ? 6 : 1)) {
      mid += 20;
      end += 30;
    }
  }

  Bitboard queens = pos.pieces(makePiece(us, PieceType::Queen));
  while (queens) {
    const Square from = bb::lsbSquare(queens);
    queens &= queens - 1;
    const Bitboard attacks =
        bb::slidingAttacks(from, occ, true) | bb::slidingAttacks(from, occ, false);
    const int mob = bb::popcount(attacks & ~friendly);
    mid += mob * 1;
    end += mob * 2;
  }

  for (int file = 0; file < 8; ++file) {
    const Bitboard fileMask = 0x0101010101010101ULL << file;
    const Bitboard pawnsOnFile = ourPawns & fileMask;
    const int count = bb::popcount(pawnsOnFile);
    if (count >= 2) {
      mid -= 12 * (count - 1);
      end -= 20 * (count - 1);
    }
    if (count >= 1) {
      const Bitboard neighbors =
          (file > 0 ? (0x0101010101010101ULL << (file - 1)) : 0) |
          (file < 7 ? (0x0101010101010101ULL << (file + 1)) : 0);
      if (!(ourPawns & neighbors)) {
        mid -= 15 * count;
        end -= 20 * count;
      }
    }
  }

  Bitboard pawns = ourPawns;
  while (pawns) {
    const Square sq = bb::lsbSquare(pawns);
    pawns &= pawns - 1;
    const int f = fileOf(sq);
    const int r = rankOf(sq);
    Bitboard frontSpan = 0;
    if (us == Color::White) {
      for (int rr = r + 1; rr < 8; ++rr) {
        for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff) {
          frontSpan |= 1ULL << static_cast<int>(squareFrom(ff, rr));
        }
      }
    } else {
      for (int rr = r - 1; rr >= 0; --rr) {
        for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff) {
          frontSpan |= 1ULL << static_cast<int>(squareFrom(ff, rr));
        }
      }
    }
    if (!(theirPawns & frontSpan)) {
      const int relRank = us == Color::White ? r : 7 - r;
      mid += kPassedBonus[relRank];
      end += kPassedBonus[relRank] * 2;
    }
  }

  Bitboard kings = pos.pieces(makePiece(us, PieceType::King));
  if (kings) {
    const Square ksq = bb::lsbSquare(kings);
    const int kf = fileOf(ksq);
    const int kr = rankOf(ksq);
    int shield = 0;
    for (int df = -1; df <= 1; ++df) {
      const int f = kf + df;
      if (f < 0 || f > 7) continue;
      const int r = us == Color::White ? kr + 1 : kr - 1;
      if (r >= 0 && r < 8) {
        if (pos.pieceOn(squareFrom(f, r)) == makePiece(us, PieceType::Pawn)) shield += 12;
      }
      const int r2 = us == Color::White ? kr + 2 : kr - 2;
      if (r2 >= 0 && r2 < 8) {
        if (pos.pieceOn(squareFrom(f, r2)) == makePiece(us, PieceType::Pawn)) shield += 6;
      }
      const Bitboard fileMask = 0x0101010101010101ULL << f;
      if (!(ourPawns & fileMask)) mid -= 10;
    }
    mid += shield;

    Bitboard ring = bb::kingAttacks[static_cast<int>(ksq)];
    int attacks = 0;
    while (ring) {
      const Square s = bb::lsbSquare(ring);
      ring &= ring - 1;
      if (bb::attackersTo(s, them, pos.pieceBoards().data(), occ)) ++attacks;
    }
    mid -= attacks * 6;
  }

  return taper(mid, end, phase);
}

}  // namespace

int evaluate(const Position& pos) {
  int phase = 0;
  for (int sq = 0; sq < 64; ++sq) {
    const Piece p = pos.pieceOn(static_cast<Square>(sq));
    if (p == Piece::None) continue;
    phase += kPhasePiece[static_cast<int>(pieceType(p))];
  }
  phase = std::min(phase, 24);

  const int score = evalColor(pos, Color::White, phase) - evalColor(pos, Color::Black, phase);
  return pos.sideToMove() == Color::White ? score : -score;
}

}  // namespace chess
