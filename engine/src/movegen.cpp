#include "chess/movegen.hpp"

#include "chess/bitboard.hpp"

#include <algorithm>

namespace chess {

namespace {

constexpr int kCastleWK = 1;
constexpr int kCastleWQ = 2;
constexpr int kCastleBK = 4;
constexpr int kCastleBQ = 8;

void addMove(std::vector<Move>& moves, Move m) { moves.push_back(m); }

Bitboard forwardPawns(Color c, Bitboard pawns) {
  return c == Color::White ? bb::northOne(pawns) : bb::southOne(pawns);
}

Bitboard pawnSinglePush(Color c, Bitboard pawns, Bitboard empty) {
  const Bitboard pushed = forwardPawns(c, pawns);
  return pushed & empty;
}

Bitboard pawnDoublePush(Color c, Bitboard pawns, Bitboard empty) {
  const Bitboard rank3 = c == Color::White ? 0x0000000000FF0000ULL : 0x0000FF0000000000ULL;
  const Bitboard single = pawnSinglePush(c, pawns, empty);
  return forwardPawns(c, single & rank3) & empty;
}

void generatePseudo(Position& pos, std::vector<Move>& moves) {
  moves.clear();
  const Color us = pos.sideToMove();
  const Color them = opposite(us);
  const Bitboard occ = pos.occ();
  const Bitboard empty = ~occ;

  const int usIdx = us == Color::White ? 0 : 6;
  const Bitboard ourPawns = pos.pieces(static_cast<Piece>(usIdx + 1));
  const Bitboard ourKnights = pos.pieces(static_cast<Piece>(usIdx + 2));
  const Bitboard ourBishops = pos.pieces(static_cast<Piece>(usIdx + 3));
  const Bitboard ourRooks = pos.pieces(static_cast<Piece>(usIdx + 4));
  const Bitboard ourQueens = pos.pieces(static_cast<Piece>(usIdx + 5));
  const Bitboard ourKings = pos.pieces(static_cast<Piece>(usIdx + 6));

  Bitboard bb = ourPawns;
  while (bb) {
    const Square from = bb::lsbSquare(bb);
    bb &= bb - 1;
    const Bitboard toSingle =
        pawnSinglePush(us, 1ULL << static_cast<int>(from), empty);
    if (toSingle) {
      const Square to = bb::lsbSquare(toSingle);
      if (rankOf(to) == (us == Color::White ? 7 : 0)) {
        for (PieceType pt :
             {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
          Move m{from, to, pt, MoveFlag::Promotion, Piece::None};
          addMove(moves, m);
        }
      } else {
        addMove(moves, Move{from, to, PieceType::None, MoveFlag::Quiet, Piece::None});
      }
    }
    const Bitboard toDouble =
        pawnDoublePush(us, 1ULL << static_cast<int>(from), empty);
    if (toDouble) {
      addMove(moves, Move{from, bb::lsbSquare(toDouble), PieceType::None,
                          MoveFlag::DoublePawnPush, Piece::None});
    }

    const Bitboard enemyOcc =
        occ & ~(pos.pieces(static_cast<Piece>(usIdx + 1)) |
                pos.pieces(static_cast<Piece>(usIdx + 2)) |
                pos.pieces(static_cast<Piece>(usIdx + 3)) |
                pos.pieces(static_cast<Piece>(usIdx + 4)) |
                pos.pieces(static_cast<Piece>(usIdx + 5)) |
                pos.pieces(static_cast<Piece>(usIdx + 6)));
    Bitboard attacks =
        bb::pawnAttacks[static_cast<int>(us)][static_cast<int>(from)] & enemyOcc;
    while (attacks) {
      const Square to = bb::lsbSquare(attacks);
      attacks &= attacks - 1;
      if (rankOf(to) == (us == Color::White ? 7 : 0)) {
        for (PieceType pt :
             {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight}) {
          addMove(moves, Move{from, to, pt, MoveFlag::PromotionCapture,
                              pos.pieceOn(to)});
        }
      } else {
        addMove(moves, Move{from, to, PieceType::None, MoveFlag::Capture,
                            pos.pieceOn(to)});
      }
    }

  }

  if (pos.epSquare() != Square::None) {
    const Square ep = pos.epSquare();
    const int epFile = fileOf(ep);
    const int epRank = rankOf(ep);
    for (int df : {-1, 1}) {
      const int f = epFile + df;
      if (f < 0 || f > 7) continue;
      const int pawnRank = us == Color::White ? epRank - 1 : epRank + 1;
      if (pawnRank < 0 || pawnRank > 7) continue;
      const Square from = squareFrom(f, pawnRank);
      if (pos.pieceOn(from) == makePiece(us, PieceType::Pawn)) {
        addMove(moves, Move{from, ep, PieceType::None, MoveFlag::EnPassant,
                            makePiece(them, PieceType::Pawn)});
      }
    }
  }

  bb = ourKnights;
  while (bb) {
    const Square from = bb::lsbSquare(bb);
    bb &= bb - 1;
    Bitboard attacks = bb::knightAttacks[static_cast<int>(from)] & ~pos.pieces(static_cast<Piece>(usIdx + 1)) &
                       ~pos.pieces(static_cast<Piece>(usIdx + 2)) &
                       ~pos.pieces(static_cast<Piece>(usIdx + 3)) &
                       ~pos.pieces(static_cast<Piece>(usIdx + 4)) &
                       ~pos.pieces(static_cast<Piece>(usIdx + 5)) &
                       ~pos.pieces(static_cast<Piece>(usIdx + 6));
    // simpler: attacks & ~friendly
    attacks = bb::knightAttacks[static_cast<int>(from)] & ~(
        pos.pieces(static_cast<Piece>(usIdx + 1)) |
        pos.pieces(static_cast<Piece>(usIdx + 2)) |
        pos.pieces(static_cast<Piece>(usIdx + 3)) |
        pos.pieces(static_cast<Piece>(usIdx + 4)) |
        pos.pieces(static_cast<Piece>(usIdx + 5)) |
        pos.pieces(static_cast<Piece>(usIdx + 6)));
    while (attacks) {
      const Square to = bb::lsbSquare(attacks);
      attacks &= attacks - 1;
      const Piece cap = pos.pieceOn(to);
      addMove(moves, Move{from, to, PieceType::None,
                          cap != Piece::None ? MoveFlag::Capture : MoveFlag::Quiet,
                          cap});
    }
  }

  auto genSlider = [&](Bitboard pieces, bool bishop) {
    Bitboard s = pieces;
    while (s) {
      const Square from = bb::lsbSquare(s);
      s &= s - 1;
      Bitboard attacks = bb::slidingAttacks(from, occ, bishop);
      attacks &= ~(pos.pieces(static_cast<Piece>(usIdx + 1)) |
                   pos.pieces(static_cast<Piece>(usIdx + 2)) |
                   pos.pieces(static_cast<Piece>(usIdx + 3)) |
                   pos.pieces(static_cast<Piece>(usIdx + 4)) |
                   pos.pieces(static_cast<Piece>(usIdx + 5)) |
                   pos.pieces(static_cast<Piece>(usIdx + 6)));
      while (attacks) {
        const Square to = bb::lsbSquare(attacks);
        attacks &= attacks - 1;
        const Piece cap = pos.pieceOn(to);
        addMove(moves, Move{from, to, PieceType::None,
                            cap != Piece::None ? MoveFlag::Capture : MoveFlag::Quiet,
                            cap});
      }
    }
  };

  genSlider(ourBishops, true);
  genSlider(ourRooks, false);

  Bitboard queens = ourQueens;
  while (queens) {
    const Square from = bb::lsbSquare(queens);
    queens &= queens - 1;
    Bitboard attacks = bb::slidingAttacks(from, occ, true) | bb::slidingAttacks(from, occ, false);
    attacks &= ~(pos.pieces(static_cast<Piece>(usIdx + 1)) |
                 pos.pieces(static_cast<Piece>(usIdx + 2)) |
                 pos.pieces(static_cast<Piece>(usIdx + 3)) |
                 pos.pieces(static_cast<Piece>(usIdx + 4)) |
                 pos.pieces(static_cast<Piece>(usIdx + 5)) |
                 pos.pieces(static_cast<Piece>(usIdx + 6)));
    while (attacks) {
      const Square to = bb::lsbSquare(attacks);
      attacks &= attacks - 1;
      const Piece cap = pos.pieceOn(to);
      addMove(moves, Move{from, to, PieceType::None,
                          cap != Piece::None ? MoveFlag::Capture : MoveFlag::Quiet, cap});
    }
  }

  bb = ourKings;
  while (bb) {
    const Square from = bb::lsbSquare(bb);
    bb &= bb - 1;
    Bitboard attacks = bb::kingAttacks[static_cast<int>(from)] &
                       ~(pos.pieces(static_cast<Piece>(usIdx + 1)) |
                         pos.pieces(static_cast<Piece>(usIdx + 2)) |
                         pos.pieces(static_cast<Piece>(usIdx + 3)) |
                         pos.pieces(static_cast<Piece>(usIdx + 4)) |
                         pos.pieces(static_cast<Piece>(usIdx + 5)) |
                         pos.pieces(static_cast<Piece>(usIdx + 6)));
    while (attacks) {
      const Square to = bb::lsbSquare(attacks);
      attacks &= attacks - 1;
      const Piece cap = pos.pieceOn(to);
      addMove(moves, Move{from, to, PieceType::None,
                          cap != Piece::None ? MoveFlag::Capture : MoveFlag::Quiet,
                          cap});
    }

    if (!pos.inCheck(us)) {
      const int rights = pos.castlingRights();
      if (us == Color::White && from == Square::E1) {
        if ((rights & kCastleWK) && !(occ & ((1ULL << static_cast<int>(Square::F1)) |
                                             (1ULL << static_cast<int>(Square::G1)))) &&
            !bb::attackersTo(Square::F1, them, pos.pieceBoards().data(), occ) &&
            !bb::attackersTo(Square::G1, them, pos.pieceBoards().data(), occ)) {
          addMove(moves, Move{Square::E1, Square::G1, PieceType::None, MoveFlag::Castle,
                              Piece::None});
        }
        if ((rights & kCastleWQ) && !(occ & ((1ULL << static_cast<int>(Square::D1)) |
                                             (1ULL << static_cast<int>(Square::C1)) |
                                             (1ULL << static_cast<int>(Square::B1)))) &&
            !bb::attackersTo(Square::D1, them, pos.pieceBoards().data(), occ) &&
            !bb::attackersTo(Square::C1, them, pos.pieceBoards().data(), occ)) {
          addMove(moves, Move{Square::E1, Square::C1, PieceType::None, MoveFlag::Castle,
                              Piece::None});
        }
      }
      if (us == Color::Black && from == Square::E8) {
        if ((rights & kCastleBK) && !(occ & ((1ULL << static_cast<int>(Square::F8)) |
                                             (1ULL << static_cast<int>(Square::G8)))) &&
            !bb::attackersTo(Square::F8, them, pos.pieceBoards().data(), occ) &&
            !bb::attackersTo(Square::G8, them, pos.pieceBoards().data(), occ)) {
          addMove(moves, Move{Square::E8, Square::G8, PieceType::None, MoveFlag::Castle,
                              Piece::None});
        }
        if ((rights & kCastleBQ) && !(occ & ((1ULL << static_cast<int>(Square::D8)) |
                                             (1ULL << static_cast<int>(Square::C8)) |
                                             (1ULL << static_cast<int>(Square::B8)))) &&
            !bb::attackersTo(Square::D8, them, pos.pieceBoards().data(), occ) &&
            !bb::attackersTo(Square::C8, them, pos.pieceBoards().data(), occ)) {
          addMove(moves, Move{Square::E8, Square::C8, PieceType::None, MoveFlag::Castle,
                              Piece::None});
        }
      }
    }
  }
}

}  // namespace

void MoveGen::generatePseudoLegal(Position& pos, std::vector<Move>& moves) {
  generatePseudo(pos, moves);
}

void MoveGen::generateLegal(Position& pos, std::vector<Move>& moves) {
  generatePseudo(pos, moves);
  std::vector<Move> legal;
  legal.reserve(moves.size());
  Undo undo;
  for (const Move& m : moves) {
    pos.makeMove(m, undo);
    if (!pos.inCheck(opposite(pos.sideToMove()))) {
      legal.push_back(m);
    }
    pos.unmakeMove(m, undo);
  }
  moves.swap(legal);
}

int perft(Position& pos, int depth) {
  if (depth == 0) return 1;
  std::vector<Move> moves;
  MoveGen::generateLegal(pos, moves);
  if (depth == 1) return static_cast<int>(moves.size());

  int nodes = 0;
  Undo undo;
  for (const Move& m : moves) {
    pos.makeMove(m, undo);
    nodes += perft(pos, depth - 1);
    pos.unmakeMove(m, undo);
  }
  return nodes;
}

}  // namespace chess
