#pragma once

#include "chess/types.hpp"

#include <array>
#include <string>
#include <vector>

namespace chess {

struct NullUndo {
  Square epSquare = Square::None;
  Bitboard hash = 0;
};

class Position {
 public:
  Position();

  bool setFromFen(const std::string& fen);
  std::string toFen() const;

  void makeMove(const Move& move, Undo& undo);
  void unmakeMove(const Move& move, const Undo& undo);
  void makeNullMove(NullUndo& undo);
  void unmakeNullMove(const NullUndo& undo);

  Color sideToMove() const { return stm_; }
  Square epSquare() const { return epSquare_; }
  int castlingRights() const { return castling_; }
  Bitboard hash() const { return hash_; }

  Piece pieceOn(Square sq) const;
  Bitboard pieces(Piece p) const { return pieces_[static_cast<int>(p)]; }
  Bitboard occ() const { return occ_; }

  bool inCheck(Color c) const;
  bool isDraw() const;
  bool hasNonPawnMaterial(Color c) const;
  int see(const Move& move) const;
  const std::array<Bitboard, 13>& pieceBoards() const { return pieces_; }

 private:
  void refreshOcc();
  void xorPiece(Square sq, Piece p);

  std::array<Bitboard, 13> pieces_{};
  Bitboard occ_ = 0;
  Color stm_ = Color::White;
  Square epSquare_ = Square::None;
  int castling_ = 0;
  int halfmove_ = 0;
  int fullmove_ = 1;
  Bitboard hash_ = 0;

  friend class MoveGen;
  friend int perft(Position& pos, int depth);
};

}  // namespace chess
