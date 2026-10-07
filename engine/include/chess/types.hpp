#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace chess {

enum class Color : uint8_t { White = 0, Black = 1 };

inline Color opposite(Color c) {
  return c == Color::White ? Color::Black : Color::White;
}

enum class PieceType : uint8_t {
  None = 0,
  Pawn,
  Knight,
  Bishop,
  Rook,
  Queen,
  King,
};

enum class Piece : uint8_t {
  None = 0,
  WPawn,
  WKnight,
  WBishop,
  WRook,
  WQueen,
  WKing,
  BPawn,
  BKnight,
  BBishop,
  BRook,
  BQueen,
  BKing,
};

constexpr Piece makePiece(Color c, PieceType pt) {
  if (pt == PieceType::None) return Piece::None;
  const int base = c == Color::White ? 1 : 7;
  return static_cast<Piece>(base + static_cast<int>(pt) - 1);
}

constexpr Color pieceColor(Piece p) {
  return static_cast<int>(p) <= static_cast<int>(Piece::WKing) ? Color::White
                                                                 : Color::Black;
}

constexpr PieceType pieceType(Piece p) {
  if (p == Piece::None) return PieceType::None;
  const int v = static_cast<int>(p);
  const int t = v <= 6 ? v : v - 6;
  return static_cast<PieceType>(t);
}

enum class Square : int {
  A1 = 0,
  B1,
  C1,
  D1,
  E1,
  F1,
  G1,
  H1,
  A2,
  B2,
  C2,
  D2,
  E2,
  F2,
  G2,
  H2,
  A3,
  B3,
  C3,
  D3,
  E3,
  F3,
  G3,
  H3,
  A4,
  B4,
  C4,
  D4,
  E4,
  F4,
  G4,
  H4,
  A5,
  B5,
  C5,
  D5,
  E5,
  F5,
  G5,
  H5,
  A6,
  B6,
  C6,
  D6,
  E6,
  F6,
  G6,
  H6,
  A7,
  B7,
  C7,
  D7,
  E7,
  F7,
  G7,
  H7,
  A8,
  B8,
  C8,
  D8,
  E8,
  F8,
  G8,
  H8,
  None = 64,
};

constexpr int fileOf(Square sq) { return static_cast<int>(sq) & 7; }
constexpr int rankOf(Square sq) { return static_cast<int>(sq) >> 3; }

constexpr Square squareFrom(int file, int rank) {
  return static_cast<Square>(rank * 8 + file);
}

using Bitboard = uint64_t;

enum class MoveFlag : uint8_t {
  Quiet = 0,
  Capture,
  DoublePawnPush,
  EnPassant,
  Castle,
  Promotion,
  PromotionCapture,
};

struct Move {
  Square from = Square::None;
  Square to = Square::None;
  PieceType promotion = PieceType::None;
  MoveFlag flag = MoveFlag::Quiet;
  Piece captured = Piece::None;

  bool operator==(const Move& other) const {
    return from == other.from && to == other.to &&
           promotion == other.promotion && flag == other.flag;
  }
};

struct Undo {
  Move move;
  Piece captured;
  PieceType capturedType;
  Square epSquare;
  int castlingRights;
  int halfmoveClock;
  Bitboard hash;
};

char pieceToChar(Piece p);
std::optional<Piece> charToPiece(char c);
std::string squareToString(Square sq);
std::optional<Square> parseSquare(std::string_view s);
std::string moveToUci(const Move& m);
std::optional<Move> uciToMove(std::string_view uci);

}  // namespace chess
