#include "chess/eval.hpp"

#include "chess/bitboard.hpp"
#include "chess/nnue.hpp"
#include "chess/pawnhash.hpp"

#include <algorithm>
#include <atomic>

namespace chess {

namespace {

std::atomic<bool> gPawnHashEnabled{true};

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
constexpr int kBackwardMg = 10;
constexpr int kBackwardEg = 16;
constexpr int kOutpostKnightMg = 28;
constexpr int kOutpostKnightEg = 18;
constexpr int kOutpostBishopMg = 18;
constexpr int kOutpostBishopEg = 12;
constexpr int kStormBonus[8] = {0, 0, 0, 8, 16, 28, 40, 0};
constexpr int kHangingMg[] = {0, 0, 40, 45, 70, 110, 0};
constexpr int kHangingEg[] = {0, 0, 28, 32, 50, 80, 0};
constexpr int kPasserKingProxEg = 8;
constexpr int kRookBehindPasserMg = 12;
constexpr int kRookBehindPasserEg = 28;

int chebyshev(Square a, Square b) {
  return std::max(std::abs(fileOf(a) - fileOf(b)), std::abs(rankOf(a) - rankOf(b)));
}

bool isPassedPawn(Color us, Square sq, Bitboard theirPawns) {
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
  return !(theirPawns & frontSpan);
}

bool isOutpostSquare(Color us, Square sq, Bitboard ourPawns, Bitboard theirPawns) {
  const int f = fileOf(sq);
  const int r = rankOf(sq);
  const int relRank = us == Color::White ? r : 7 - r;
  if (relRank < 4) return false;
  // Must be protected by our pawn.
  if (!(bb::pawnAttackers[static_cast<int>(us)][static_cast<int>(sq)] & ourPawns)) return false;
  // No enemy pawn can attack this square (adjacent files ahead).
  Bitboard leverage = 0;
  if (us == Color::White) {
    for (int rr = r + 1; rr < 8; ++rr) {
      if (f > 0) leverage |= 1ULL << static_cast<int>(squareFrom(f - 1, rr));
      if (f < 7) leverage |= 1ULL << static_cast<int>(squareFrom(f + 1, rr));
    }
  } else {
    for (int rr = r - 1; rr >= 0; --rr) {
      if (f > 0) leverage |= 1ULL << static_cast<int>(squareFrom(f - 1, rr));
      if (f < 7) leverage |= 1ULL << static_cast<int>(squareFrom(f + 1, rr));
    }
  }
  return !(theirPawns & leverage);
}

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

void evalPawnStructure(const Position& pos, Color us, int& mid, int& end) {
  const Color them = opposite(us);
  mid = 0;
  end = 0;

  const Bitboard ourPawns = pos.pieces(makePiece(us, PieceType::Pawn));
  const Bitboard theirPawns = pos.pieces(makePiece(them, PieceType::Pawn));

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
      mid += kCandidateBonus[relRank];
      end += kCandidateBonus[relRank] * 2;
    }

    // Backward: no friendly pawn support on adjacent files at/behind this rank,
    // and the stop square is controlled by an enemy pawn.
    if (!passed && relRank >= 1 && relRank <= 5) {
      Bitboard support = 0;
      if (us == Color::White) {
        for (int rr = 0; rr <= r; ++rr) {
          if (f > 0) support |= 1ULL << static_cast<int>(squareFrom(f - 1, rr));
          if (f < 7) support |= 1ULL << static_cast<int>(squareFrom(f + 1, rr));
        }
      } else {
        for (int rr = r; rr < 8; ++rr) {
          if (f > 0) support |= 1ULL << static_cast<int>(squareFrom(f - 1, rr));
          if (f < 7) support |= 1ULL << static_cast<int>(squareFrom(f + 1, rr));
        }
      }
      if (!(ourPawns & support)) {
        const int stopR = us == Color::White ? r + 1 : r - 1;
        if (stopR >= 0 && stopR < 8) {
          const Square stop = squareFrom(f, stopR);
          if (bb::pawnAttacks[static_cast<int>(them)][static_cast<int>(stop)] & theirPawns) {
            mid -= kBackwardMg;
            end -= kBackwardEg;
          }
        }
      }
    }
  }
}

// Passer terms that depend on kings/rooks — kept outside the pawn hash.
void evalPassedExtras(const Position& pos, Color us, int& mid, int& end) {
  mid = 0;
  end = 0;
  const Color them = opposite(us);
  const Bitboard ourPawns = pos.pieces(makePiece(us, PieceType::Pawn));
  const Bitboard theirPawns = pos.pieces(makePiece(them, PieceType::Pawn));
  const Bitboard ourRooks = pos.pieces(makePiece(us, PieceType::Rook));
  const Bitboard ourKings = pos.pieces(makePiece(us, PieceType::King));
  const Bitboard theirKings = pos.pieces(makePiece(them, PieceType::King));
  if (!ourKings || !theirKings) return;
  const Square ourKing = bb::lsbSquare(ourKings);
  const Square theirKing = bb::lsbSquare(theirKings);

  Bitboard pawns = ourPawns;
  while (pawns) {
    const Square sq = bb::lsbSquare(pawns);
    pawns &= pawns - 1;
    if (!isPassedPawn(us, sq, theirPawns)) continue;

    const int f = fileOf(sq);
    const int r = rankOf(sq);
    const int relRank = us == Color::White ? r : 7 - r;
    // King proximity: reward our king nearer and theirs farther (EG).
    const int ourDist = chebyshev(ourKing, sq);
    const int theirDist = chebyshev(theirKing, sq);
    end += kPasserKingProxEg * (theirDist - ourDist) * (relRank + 1) / 4;

    // Rook behind passer on the same file.
    const Bitboard fileMask = 0x0101010101010101ULL << f;
    Bitboard behind = 0;
    if (us == Color::White) {
      for (int rr = 0; rr < r; ++rr) behind |= 1ULL << static_cast<int>(squareFrom(f, rr));
    } else {
      for (int rr = r + 1; rr < 8; ++rr) behind |= 1ULL << static_cast<int>(squareFrom(f, rr));
    }
    if (ourRooks & behind & fileMask) {
      mid += kRookBehindPasserMg;
      end += kRookBehindPasserEg;
    }
  }
}

void evalNonPawnPositional(const Position& pos, Color us, int& mid, int& end) {
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
    if (isOutpostSquare(us, from, ourPawns, theirPawns)) {
      mid += kOutpostKnightMg;
      end += kOutpostKnightEg;
    }
  }

  Bitboard bishops = pos.pieces(makePiece(us, PieceType::Bishop));
  while (bishops) {
    const Square from = bb::lsbSquare(bishops);
    bishops &= bishops - 1;
    const int mob = bb::popcount(bb::slidingAttacks(from, occ, true) & ~friendly);
    mid += mob * 3;
    end += mob * 3;
    if (isOutpostSquare(us, from, ourPawns, theirPawns)) {
      mid += kOutpostBishopMg;
      end += kOutpostBishopEg;
    }
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

  // Hanging / undefended non-pawn pieces attacked by the opponent.
  const PieceType hangTypes[] = {PieceType::Knight, PieceType::Bishop, PieceType::Rook,
                                 PieceType::Queen};
  for (PieceType pt : hangTypes) {
    Bitboard bb = pos.pieces(makePiece(us, pt));
    while (bb) {
      const Square sq = bb::lsbSquare(bb);
      bb &= bb - 1;
      const Bitboard attacked = bb::attackersTo(sq, them, pos.pieceBoards().data(), occ);
      if (!attacked) continue;
      const Bitboard defended = bb::attackersTo(sq, us, pos.pieceBoards().data(), occ);
      if (defended) continue;
      const int idx = static_cast<int>(pt);
      mid -= kHangingMg[idx];
      end -= kHangingEg[idx];
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

  // Pawn storm toward the enemy king.
  const Bitboard theirKings = pos.pieces(makePiece(them, PieceType::King));
  if (theirKings) {
    const Square ek = bb::lsbSquare(theirKings);
    const int ekf = fileOf(ek);
    Bitboard stormPawns = ourPawns;
    while (stormPawns) {
      const Square sq = bb::lsbSquare(stormPawns);
      stormPawns &= stormPawns - 1;
      const int f = fileOf(sq);
      if (std::abs(f - ekf) > 1) continue;
      const int relRank = us == Color::White ? rankOf(sq) : 7 - rankOf(sq);
      mid += kStormBonus[relRank];
    }
  }
}

void pawnStructureWhiteMinusBlack(const Position& pos, int& mid, int& end) {
  mid = 0;
  end = 0;
  if (gPawnHashEnabled.load(std::memory_order_relaxed)) {
    const Bitboard key = pawnKey(pos);
    PawnHash& ph = threadPawnHash();
    if (ph.probe(key, mid, end)) return;
    int wMid = 0, wEnd = 0, bMid = 0, bEnd = 0;
    evalPawnStructure(pos, Color::White, wMid, wEnd);
    evalPawnStructure(pos, Color::Black, bMid, bEnd);
    mid = wMid - bMid;
    end = wEnd - bEnd;
    ph.store(key, mid, end);
    return;
  }
  int wMid = 0, wEnd = 0, bMid = 0, bEnd = 0;
  evalPawnStructure(pos, Color::White, wMid, wEnd);
  evalPawnStructure(pos, Color::Black, bMid, bEnd);
  mid = wMid - bMid;
  end = wEnd - bEnd;
}

int evaluateFromParts(int scoreMg, int scoreEg, int phase, const Position& pos) {
  phase = std::min(std::max(phase, 0), 24);

  int pMid = 0, pEnd = 0;
  pawnStructureWhiteMinusBlack(pos, pMid, pEnd);

  int passWMid = 0, passWEnd = 0, passBMid = 0, passBEnd = 0;
  evalPassedExtras(pos, Color::White, passWMid, passWEnd);
  evalPassedExtras(pos, Color::Black, passBMid, passBEnd);

  int wMid = 0, wEnd = 0, bMid = 0, bEnd = 0;
  evalNonPawnPositional(pos, Color::White, wMid, wEnd);
  evalNonPawnPositional(pos, Color::Black, bMid, bEnd);

  int mid = scoreMg + pMid + passWMid - passBMid + wMid - bMid;
  int end = scoreEg + pEnd + passWEnd - passBEnd + wEnd - bEnd;

  if (pos.sideToMove() == Color::White) {
    mid += kTempoMg;
    end += kTempoEg;
  } else {
    mid -= kTempoMg;
    end -= kTempoEg;
  }

  const int scale = endgameScaleFactor(pos);
  if (scale < 128) end = end * scale / 128;

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

bool setPawnHashEnabled(bool on) {
  return gPawnHashEnabled.exchange(on);
}

int endgameScaleFactor(const Position& pos) {
  const Bitboard wp = pos.pieces(Piece::WPawn);
  const Bitboard bp = pos.pieces(Piece::BPawn);
  const Bitboard wn = pos.pieces(Piece::WKnight);
  const Bitboard bn = pos.pieces(Piece::BKnight);
  const Bitboard wb = pos.pieces(Piece::WBishop);
  const Bitboard blackB = pos.pieces(Piece::BBishop);
  const Bitboard wr = pos.pieces(Piece::WRook);
  const Bitboard br = pos.pieces(Piece::BRook);
  const Bitboard wq = pos.pieces(Piece::WQueen);
  const Bitboard bq = pos.pieces(Piece::BQueen);

  const int wP = bb::popcount(wp);
  const int bP = bb::popcount(bp);
  const int wN = bb::popcount(wn);
  const int bN = bb::popcount(bn);
  const int wB = bb::popcount(wb);
  const int bB = bb::popcount(blackB);
  const int wR = bb::popcount(wr);
  const int bR = bb::popcount(br);
  const int wQ = bb::popcount(wq);
  const int bQ = bb::popcount(bq);

  // Opposite bishops: one bishop each, no other non-pawn pieces.
  if (wB == 1 && bB == 1 && wN + bN + wR + bR + wQ + bQ == 0) {
    const Square ws = bb::lsbSquare(wb);
    const Square bs = bb::lsbSquare(blackB);
    const int wColor = (fileOf(ws) + rankOf(ws)) & 1;
    const int bColor = (fileOf(bs) + rankOf(bs)) & 1;
    if (wColor != bColor) {
      const int pawnAdv = std::abs(wP - bP);
      return pawnAdv <= 1 ? 64 : 96;
    }
  }

  // No pawns, each side at most one minor (drawish KB/KN endings).
  if (wP + bP == 0 && wR + bR + wQ + bQ == 0 && wN + wB <= 1 && bN + bB <= 1 &&
      wN + wB + bN + bB > 0) {
    return 32;
  }

  return 128;
}

int evaluateClassical(const Position& pos) {
  return evaluateFromParts(pos.scoreMg(), pos.scoreEg(), pos.phase(), pos);
}

int evaluate(const Position& pos) {
  if (nnue::useNnue()) {
    int score = nnue::evaluate(pos, pos.nnueAcc());
    const int scale = endgameScaleFactor(pos);
    if (scale < 128) score = score * scale / 128;
    return score;
  }
  return evaluateClassical(pos);
}

int evaluateFull(const Position& pos) {
  int mg = 0, eg = 0, phase = 0;
  computeMaterial(pos, mg, eg, phase);
  return evaluateFromParts(mg, eg, phase, pos);
}

}  // namespace chess
