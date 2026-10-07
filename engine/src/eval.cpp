#include "chess/eval.hpp"

#include "chess/bitboard.hpp"

#include <algorithm>

namespace chess {

namespace {

constexpr int kPieceValue[] = {0, 100, 320, 330, 500, 900, 20000};
constexpr int kPhasePiece[] = {0, 0, 1, 1, 2, 4, 0};  // N B R Q
constexpr int kTempoMg = 15;
constexpr int kTempoEg = 5;

constexpr int kPstPawnMg[64] = {
    0,   0,   0,   0,   0,   0,   0,  0,
    5,  10,  10, -20, -20,  10,  10,  5,
    5,  -5, -10,   0,   0, -10,  -5,  5,
    0,   0,   0,  20,  20,   0,   0,  0,
    5,   5,  10,  25,  25,  10,   5,  5,
   10,  10,  20,  30,  30,  20,  10, 10,
   50,  50,  50,  50,  50,  50,  50, 50,
    0,   0,   0,   0,   0,   0,   0,  0,
};

constexpr int kPstPawnEg[64] = {
    0,   0,   0,   0,   0,   0,   0,  0,
   10,  10,  10,  10,  10,  10,  10, 10,
    5,   5,   5,   5,   5,   5,   5,  5,
    5,   5,   5,   5,   5,   5,   5,  5,
   15,  15,  15,  15,  15,  15,  15, 15,
   30,  30,  30,  30,  30,  30,  30, 30,
   70,  70,  70,  70,  70,  70,  70, 70,
    0,   0,   0,   0,   0,   0,   0,  0,
};

constexpr int kPstKnightMg[64] = {
  -50, -40, -30, -30, -30, -30, -40, -50,
  -40, -20,   0,   0,   0,   0, -20, -40,
  -30,   0,  10,  15,  15,  10,   0, -30,
  -30,   5,  15,  20,  20,  15,   5, -30,
  -30,   0,  15,  20,  20,  15,   0, -30,
  -30,   5,  10,  15,  15,  10,   5, -30,
  -40, -20,   0,   5,   5,   0, -20, -40,
  -50, -40, -30, -30, -30, -30, -40, -50,
};

constexpr int kPstKnightEg[64] = {
  -50, -40, -30, -30, -30, -30, -40, -50,
  -40, -20,   0,   5,   5,   0, -20, -40,
  -30,   5,  10,  15,  15,  10,   5, -30,
  -30,   5,  15,  20,  20,  15,   5, -30,
  -30,   5,  15,  20,  20,  15,   5, -30,
  -30,   5,  10,  15,  15,  10,   5, -30,
  -40, -20,   0,   5,   5,   0, -20, -40,
  -50, -40, -30, -30, -30, -30, -40, -50,
};

constexpr int kPstBishopMg[64] = {
  -20, -10, -10, -10, -10, -10, -10, -20,
  -10,   0,   0,   0,   0,   0,   0, -10,
  -10,   0,  10,  10,  10,  10,   0, -10,
  -10,   5,   5,  10,  10,   5,   5, -10,
  -10,   0,   5,  10,  10,   5,   0, -10,
  -10,  10,  10,  10,  10,  10,  10, -10,
  -10,   5,   0,   0,   0,   0,   5, -10,
  -20, -10, -10, -10, -10, -10, -10, -20,
};

constexpr int kPstBishopEg[64] = {
  -20, -10, -10, -10, -10, -10, -10, -20,
  -10,   0,   0,   0,   0,   0,   0, -10,
  -10,   0,  10,  10,  10,  10,   0, -10,
  -10,   5,   5,  15,  15,   5,   5, -10,
  -10,   0,   5,  15,  15,   5,   0, -10,
  -10,  10,  10,  10,  10,  10,  10, -10,
  -10,   5,   0,   0,   0,   0,   5, -10,
  -20, -10, -10, -10, -10, -10, -10, -20,
};

constexpr int kPstRookMg[64] = {
    0,   0,   0,   0,   0,   0,  0,   0,
    5,  10,  10,  10,  10,  10,  10,  5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
   -5,   0,   0,   0,   0,   0,  0,  -5,
    0,   0,   0,   5,   5,   0,  0,   0,
};

constexpr int kPstRookEg[64] = {
    0,   0,   0,   0,   0,   0,  0,   0,
   10,  15,  15,  15,  15,  15,  15, 10,
    0,   0,   0,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   0,  0,   0,
    0,   0,   0,   0,   0,   0,  0,   0,
    0,   0,   0,   5,   5,   0,  0,   0,
};

constexpr int kPstQueenMg[64] = {
  -20, -10, -10,  -5,  -5, -10, -10, -20,
  -10,   0,   0,   0,   0,   0,   0, -10,
  -10,   0,   5,   5,   5,   5,   0, -10,
   -5,   0,   5,   5,   5,   5,   0,  -5,
    0,   0,   5,   5,   5,   5,   0,  -5,
  -10,   5,   5,   5,   5,   5,   0, -10,
  -10,   0,   5,   0,   0,   0,   0, -10,
  -20, -10, -10,  -5,  -5, -10, -10, -20,
};

constexpr int kPstQueenEg[64] = {
  -20, -10, -10,  -5,  -5, -10, -10, -20,
  -10,   0,   5,   5,   5,   5,   0, -10,
  -10,   5,   5,   5,   5,   5,   5, -10,
   -5,   5,   5,   5,   5,   5,   5,  -5,
   -5,   5,   5,   5,   5,   5,   5,  -5,
  -10,   5,   5,   5,   5,   5,   5, -10,
  -10,   0,   5,   5,   5,   5,   0, -10,
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
constexpr int kCandidateBonus[8] = {0, 5, 10, 15, 25, 40, 0, 0};
constexpr int kConnectedPasser[8] = {0, 5, 10, 15, 25, 40, 60, 0};

int mirror(int sq, Color c) { return c == Color::White ? sq : 63 - sq; }

int pstMid(PieceType pt, int idx) {
  switch (pt) {
    case PieceType::Pawn:
      return kPstPawnMg[idx];
    case PieceType::Knight:
      return kPstKnightMg[idx];
    case PieceType::Bishop:
      return kPstBishopMg[idx];
    case PieceType::Rook:
      return kPstRookMg[idx];
    case PieceType::Queen:
      return kPstQueenMg[idx];
    case PieceType::King:
      return kPstKingMid[idx];
    default:
      return 0;
  }
}

int pstEnd(PieceType pt, int idx) {
  switch (pt) {
    case PieceType::Pawn:
      return kPstPawnEg[idx];
    case PieceType::Knight:
      return kPstKnightEg[idx];
    case PieceType::Bishop:
      return kPstBishopEg[idx];
    case PieceType::Rook:
      return kPstRookEg[idx];
    case PieceType::Queen:
      return kPstQueenEg[idx];
    case PieceType::King:
      return kPstKingEnd[idx];
    default:
      return 0;
  }
}

int taper(int mid, int end, int phase) {
  return (mid * phase + end * (24 - phase)) / 24;
}

int attackerWeight(const Position& pos, Square s, Color them, Bitboard occ) {
  int w = 0;
  const Bitboard attackers = bb::attackersTo(s, them, pos.pieceBoards().data(), occ);
  if (!attackers) return 0;
  if (attackers & pos.pieces(makePiece(them, PieceType::Queen))) w += 4;
  if (attackers & pos.pieces(makePiece(them, PieceType::Rook))) w += 2;
  if (attackers & (pos.pieces(makePiece(them, PieceType::Bishop)) |
                   pos.pieces(makePiece(them, PieceType::Knight))))
    w += 1;
  if (attackers & pos.pieces(makePiece(them, PieceType::Pawn))) w += 1;
  return w;
}

void evalPositional(const Position& pos, Color us, int& mid, int& end) {
  const Color them = opposite(us);
  const Bitboard occ = pos.occ();
  mid = 0;
  end = 0;

  const Bitboard ourPawns = pos.pieces(makePiece(us, PieceType::Pawn));
  const Bitboard theirPawns = pos.pieces(makePiece(them, PieceType::Pawn));
  const Bitboard friendly =
      pos.pieces(makePiece(us, PieceType::Pawn)) |
      pos.pieces(makePiece(us, PieceType::Knight)) |
      pos.pieces(makePiece(us, PieceType::Bishop)) |
      pos.pieces(makePiece(us, PieceType::Rook)) |
      pos.pieces(makePiece(us, PieceType::Queen)) |
      pos.pieces(makePiece(us, PieceType::King));

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
      mid -= 15 * (count - 1);
      end -= 25 * (count - 1);
    }
    if (count >= 1) {
      const Bitboard neighbors =
          (file > 0 ? (0x0101010101010101ULL << (file - 1)) : 0) |
          (file < 7 ? (0x0101010101010101ULL << (file + 1)) : 0);
      if (!(ourPawns & neighbors)) {
        mid -= 12 * count;
        end -= 18 * count;
      }
    }
  }

  Bitboard pawns = ourPawns;
  while (pawns) {
    const Square sq = bb::lsbSquare(pawns);
    pawns &= pawns - 1;
    const int f = fileOf(sq);
    const int r = rankOf(sq);
    const int relRank = us == Color::White ? r : 7 - r;

    Bitboard frontSpan = 0;
    Bitboard frontFile = 0;
    if (us == Color::White) {
      for (int rr = r + 1; rr < 8; ++rr) {
        frontFile |= 1ULL << static_cast<int>(squareFrom(f, rr));
        for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff) {
          frontSpan |= 1ULL << static_cast<int>(squareFrom(ff, rr));
        }
      }
    } else {
      for (int rr = r - 1; rr >= 0; --rr) {
        frontFile |= 1ULL << static_cast<int>(squareFrom(f, rr));
        for (int ff = std::max(0, f - 1); ff <= std::min(7, f + 1); ++ff) {
          frontSpan |= 1ULL << static_cast<int>(squareFrom(ff, rr));
        }
      }
    }

    const bool passed = !(theirPawns & frontSpan);
    if (passed) {
      mid += kPassedBonus[relRank];
      end += kPassedBonus[relRank] * 2;
      // Connected passer: friendly pawn on neighboring file at same/adjacent rank.
      const Bitboard neighbors =
          (f > 0 ? (0x0101010101010101ULL << (f - 1)) : 0) |
          (f < 7 ? (0x0101010101010101ULL << (f + 1)) : 0);
      Bitboard near = 0;
      const int sqi = static_cast<int>(sq);
      near |= 1ULL << sqi;
      if (sqi + 8 < 64) near |= 1ULL << (sqi + 8);
      if (sqi - 8 >= 0) near |= 1ULL << (sqi - 8);
      if (ourPawns & neighbors & near) {
        mid += kConnectedPasser[relRank];
        end += kConnectedPasser[relRank] * 2;
      }
    } else if (!(theirPawns & frontFile)) {
      // Candidate passer: no enemy pawn on same file ahead, but blocked on adjacent.
      mid += kCandidateBonus[relRank];
      end += kCandidateBonus[relRank] * 2;
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
        if (pos.pieceOn(squareFrom(f, r)) == makePiece(us, PieceType::Pawn)) shield += 14;
      }
      const int r2 = us == Color::White ? kr + 2 : kr - 2;
      if (r2 >= 0 && r2 < 8) {
        if (pos.pieceOn(squareFrom(f, r2)) == makePiece(us, PieceType::Pawn)) shield += 7;
      }
      const Bitboard fileMask = 0x0101010101010101ULL << f;
      if (!(ourPawns & fileMask)) mid -= 12;
    }
    mid += shield;

    Bitboard ring = bb::kingAttacks[static_cast<int>(ksq)];
    int attackScore = 0;
    while (ring) {
      const Square s = bb::lsbSquare(ring);
      ring &= ring - 1;
      attackScore += attackerWeight(pos, s, them, occ);
    }
    mid -= attackScore * 8;
    end -= attackScore * 2;
  }
}

int evaluateFromParts(int scoreMg, int scoreEg, int phase, const Position& pos) {
  phase = std::min(std::max(phase, 0), 24);

  int wMid = 0, wEnd = 0, bMid = 0, bEnd = 0;
  evalPositional(pos, Color::White, wMid, wEnd);
  evalPositional(pos, Color::Black, bMid, bEnd);

  int mid = scoreMg + wMid - bMid;
  int end = scoreEg + wEnd - bEnd;

  // Tempo: small bonus for the side to move (applied after White-POV assemble).
  if (pos.sideToMove() == Color::White) {
    mid += kTempoMg;
    end += kTempoEg;
  } else {
    mid -= kTempoMg;
    end -= kTempoEg;
  }

  const int score = taper(mid, end, phase);
  return pos.sideToMove() == Color::White ? score : -score;
}

}  // namespace

void applyMaterialDelta(Square sq, Piece p, int sign, int& mg, int& eg, int& phase) {
  if (p == Piece::None || sq == Square::None) return;
  const PieceType pt = pieceType(p);
  const Color c = pieceColor(p);
  const int idx = mirror(static_cast<int>(sq), c);
  const int mat = kPieceValue[static_cast<int>(pt)];
  const int side = c == Color::White ? 1 : -1;
  const int delta = sign * side;
  mg += delta * (mat + pstMid(pt, idx));
  eg += delta * (mat + pstEnd(pt, idx));
  phase += sign * kPhasePiece[static_cast<int>(pt)];
}

void computeMaterial(const Position& pos, int& mg, int& eg, int& phase) {
  mg = 0;
  eg = 0;
  phase = 0;
  for (int sq = 0; sq < 64; ++sq) {
    const Piece p = pos.pieceOn(static_cast<Square>(sq));
    if (p == Piece::None) continue;
    applyMaterialDelta(static_cast<Square>(sq), p, +1, mg, eg, phase);
  }
}

int evaluate(const Position& pos) {
  return evaluateFromParts(pos.scoreMg(), pos.scoreEg(), pos.phase(), pos);
}

int evaluateFull(const Position& pos) {
  int mg = 0, eg = 0, phase = 0;
  computeMaterial(pos, mg, eg, phase);
  return evaluateFromParts(mg, eg, phase, pos);
}

}  // namespace chess
