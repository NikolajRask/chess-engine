#include "chess/types.hpp"

#include <cctype>

namespace chess {

char pieceToChar(Piece p) {
  switch (p) {
    case Piece::WPawn:
    case Piece::BPawn:
      return 'p';
    case Piece::WKnight:
    case Piece::BKnight:
      return 'n';
    case Piece::WBishop:
    case Piece::BBishop:
      return 'b';
    case Piece::WRook:
    case Piece::BRook:
      return 'r';
    case Piece::WQueen:
    case Piece::BQueen:
      return 'q';
    case Piece::WKing:
    case Piece::BKing:
      return 'k';
    default:
      return '.';
  }
}

std::optional<Piece> charToPiece(char c) {
  const bool white = std::isupper(static_cast<unsigned char>(c));
  const char lc = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  Color color = white ? Color::White : Color::Black;
  PieceType pt = PieceType::None;
  switch (lc) {
    case 'p':
      pt = PieceType::Pawn;
      break;
    case 'n':
      pt = PieceType::Knight;
      break;
    case 'b':
      pt = PieceType::Bishop;
      break;
    case 'r':
      pt = PieceType::Rook;
      break;
    case 'q':
      pt = PieceType::Queen;
      break;
    case 'k':
      pt = PieceType::King;
      break;
    default:
      return std::nullopt;
  }
  return makePiece(color, pt);
}

std::string squareToString(Square sq) {
  if (sq == Square::None) return "??";
  const int f = fileOf(sq);
  const int r = rankOf(sq);
  std::string s;
  s += static_cast<char>('a' + f);
  s += static_cast<char>('1' + r);
  return s;
}

std::optional<Square> parseSquare(std::string_view s) {
  if (s.size() < 2) return std::nullopt;
  const int f = s[0] - 'a';
  const int r = s[1] - '1';
  if (f < 0 || f > 7 || r < 0 || r > 7) return std::nullopt;
  return squareFrom(f, r);
}

std::string moveToUci(const Move& m) {
  std::string uci = squareToString(m.from) + squareToString(m.to);
  if (m.promotion != PieceType::None) {
    char pc = 'q';
    switch (m.promotion) {
      case PieceType::Knight:
        pc = 'n';
        break;
      case PieceType::Bishop:
        pc = 'b';
        break;
      case PieceType::Rook:
        pc = 'r';
        break;
      case PieceType::Queen:
        pc = 'q';
        break;
      default:
        break;
    }
    uci += pc;
  }
  return uci;
}

std::optional<Move> uciToMove(std::string_view uci) {
  if (uci.size() < 4) return std::nullopt;
  auto from = parseSquare(uci.substr(0, 2));
  auto to = parseSquare(uci.substr(2, 2));
  if (!from || !to) return std::nullopt;
  Move m;
  m.from = *from;
  m.to = *to;
  if (uci.size() >= 5) {
    switch (uci[4]) {
      case 'n':
        m.promotion = PieceType::Knight;
        break;
      case 'b':
        m.promotion = PieceType::Bishop;
        break;
      case 'r':
        m.promotion = PieceType::Rook;
        break;
      case 'q':
        m.promotion = PieceType::Queen;
        break;
      default:
        return std::nullopt;
    }
    m.flag = MoveFlag::Promotion;
  }
  return m;
}

}  // namespace chess
