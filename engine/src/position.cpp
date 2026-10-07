#include "chess/position.hpp"

#include "chess/bitboard.hpp"
#include "chess/zobrist.hpp"

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace chess {

namespace {

struct InitTables {
  InitTables() {
    bb::init();
    zobrist::init();
  }
};

const InitTables kInit;

constexpr int kCastleWK = 1;
constexpr int kCastleWQ = 2;
constexpr int kCastleBK = 4;
constexpr int kCastleBQ = 8;

}  // namespace

Position::Position() { setFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); }

Piece Position::pieceOn(Square sq) const {
  if (sq == Square::None) return Piece::None;
  const Bitboard mask = 1ULL << static_cast<int>(sq);
  for (int p = 1; p <= 12; ++p) {
    if (pieces_[p] & mask) return static_cast<Piece>(p);
  }
  return Piece::None;
}

void Position::refreshOcc() {
  occ_ = 0;
  for (int p = 1; p <= 12; ++p) occ_ |= pieces_[p];
}

void Position::xorPiece(Square sq, Piece p) {
  const int idx = static_cast<int>(p);
  const Bitboard mask = 1ULL << static_cast<int>(sq);
  pieces_[idx] ^= mask;
  occ_ ^= mask;
}

bool Position::setFromFen(const std::string& fen) {
  pieces_.fill(0);
  occ_ = 0;
  epSquare_ = Square::None;
  castling_ = 0;
  halfmove_ = 0;
  fullmove_ = 1;

  std::istringstream iss(fen);
  std::string board, stm, castling, ep;
  int half = 0, full = 1;
  if (!(iss >> board >> stm)) return false;
  if (!(iss >> castling)) castling = "-";
  if (!(iss >> ep)) ep = "-";
  iss >> half >> full;
  halfmove_ = half;
  fullmove_ = full > 0 ? full : 1;

  int sq = 56;
  for (char c : board) {
    if (c == '/') {
      sq -= 16;
      continue;
    }
    if (c >= '1' && c <= '8') {
      sq += c - '0';
      continue;
    }
    auto piece = charToPiece(c);
    if (!piece) return false;
    const int idx = static_cast<int>(*piece);
    pieces_[idx] |= 1ULL << sq;
    ++sq;
  }
  refreshOcc();

  stm_ = (stm == "b") ? Color::Black : Color::White;

  if (castling != "-") {
    for (char c : castling) {
      if (c == 'K') castling_ |= kCastleWK;
      if (c == 'Q') castling_ |= kCastleWQ;
      if (c == 'k') castling_ |= kCastleBK;
      if (c == 'q') castling_ |= kCastleBQ;
    }
  }

  if (ep != "-") {
    auto sqEp = parseSquare(ep);
    if (sqEp) epSquare_ = *sqEp;
  }

  hash_ = zobrist::hashPosition(*this);
  return true;
}

std::string Position::toFen() const {
  std::ostringstream oss;
  for (int rank = 7; rank >= 0; --rank) {
    int empty = 0;
    for (int file = 0; file < 8; ++file) {
      const Square sq = squareFrom(file, rank);
      const Piece p = pieceOn(sq);
      if (p == Piece::None) {
        ++empty;
      } else {
        if (empty) {
          oss << empty;
          empty = 0;
        }
        char c = pieceToChar(p);
        if (pieceColor(p) == Color::White)
          c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        oss << c;
      }
    }
    if (empty) oss << empty;
    if (rank > 0) oss << '/';
  }
  oss << (stm_ == Color::White ? " w " : " b ");
  std::string cr;
  if (castling_ & kCastleWK) cr += 'K';
  if (castling_ & kCastleWQ) cr += 'Q';
  if (castling_ & kCastleBK) cr += 'k';
  if (castling_ & kCastleBQ) cr += 'q';
  if (cr.empty()) cr = "-";
  oss << cr << ' ';
  oss << (epSquare_ == Square::None ? "-" : squareToString(epSquare_)) << ' ';
  oss << halfmove_ << ' ' << fullmove_;
  return oss.str();
}

bool Position::inCheck(Color c) const {
  const Piece king = makePiece(c, PieceType::King);
  Bitboard kings = pieces_[static_cast<int>(king)];
  if (!kings) return false;
  const Square ksq = bb::lsbSquare(kings);
  return bb::attackersTo(ksq, opposite(c), pieces_.data(), occ_) != 0;
}

bool Position::isDraw() const { return halfmove_ >= 100; }

bool Position::hasNonPawnMaterial(Color c) const {
  const int base = c == Color::White ? 0 : 6;
  return pieces_[base + 2] | pieces_[base + 3] | pieces_[base + 4] | pieces_[base + 5];
}

void Position::makeNullMove(NullUndo& undo) {
  undo.epSquare = epSquare_;
  undo.hash = hash_;
  if (epSquare_ != Square::None) {
    hash_ ^= zobrist::epFile[fileOf(epSquare_)];
  }
  epSquare_ = Square::None;
  stm_ = opposite(stm_);
  hash_ ^= zobrist::side;
}

void Position::unmakeNullMove(const NullUndo& undo) {
  stm_ = opposite(stm_);
  epSquare_ = undo.epSquare;
  hash_ = undo.hash;
}

int Position::see(const Move& move) const {
  static constexpr int kVal[] = {0, 100, 320, 330, 500, 900, 20000};
  const Piece moving = pieceOn(move.from);
  if (moving == Piece::None) return 0;

  Piece victim = Piece::None;
  if (move.flag == MoveFlag::EnPassant) {
    victim = makePiece(opposite(stm_), PieceType::Pawn);
  } else {
    victim = pieceOn(move.to);
  }

  int gain[32];
  int d = 0;
  const Square to = move.to;
  Bitboard fromBit = 1ULL << static_cast<int>(move.from);
  Bitboard toBit = 1ULL << static_cast<int>(to);

  Bitboard pieces[13];
  for (int i = 0; i < 13; ++i) pieces[i] = pieces_[i];
  Bitboard occ = occ_;

  // Remove mover from origin.
  pieces[static_cast<int>(moving)] ^= fromBit;
  occ ^= fromBit;

  if (move.flag == MoveFlag::EnPassant) {
    const int capSq =
        static_cast<int>(to) + (stm_ == Color::White ? -8 : 8);
    const Bitboard capBit = 1ULL << capSq;
    pieces[static_cast<int>(victim)] ^= capBit;
    occ ^= capBit;
  } else if (victim != Piece::None) {
    pieces[static_cast<int>(victim)] ^= toBit;
    // Occupancy on `to` stays occupied by the capturing piece conceptually.
  }

  PieceType nextType = pieceType(moving);
  if (move.promotion != PieceType::None) nextType = move.promotion;

  gain[d] = victim == Piece::None ? 0 : kVal[static_cast<int>(pieceType(victim))];
  if (move.promotion != PieceType::None) {
    gain[d] += kVal[static_cast<int>(move.promotion)] - kVal[static_cast<int>(PieceType::Pawn)];
  }

  Color side = opposite(stm_);
  for (;;) {
    Bitboard attackers = bb::attackersTo(to, side, pieces, occ);
    // Exclude pieces still on the target square from "attacking" themselves.
    attackers &= ~toBit;
    if (!attackers) break;

    PieceType pt = PieceType::None;
    Bitboard attackerBb = 0;
    for (PieceType cand :
         {PieceType::Pawn, PieceType::Knight, PieceType::Bishop, PieceType::Rook,
          PieceType::Queen, PieceType::King}) {
      const Piece p = makePiece(side, cand);
      attackerBb = attackers & pieces[static_cast<int>(p)];
      if (attackerBb) {
        pt = cand;
        break;
      }
    }
    if (pt == PieceType::None) break;

    const Square from = bb::lsbSquare(attackerBb);
    const Bitboard bit = 1ULL << static_cast<int>(from);
    pieces[static_cast<int>(makePiece(side, pt))] ^= bit;
    occ ^= bit;

    ++d;
    gain[d] = kVal[static_cast<int>(nextType)] - gain[d - 1];
    if (std::max(-gain[d - 1], gain[d]) < 0) break;
    nextType = pt;
    side = opposite(side);
  }

  while (d > 0) {
    --d;
    gain[d] = -std::max(-gain[d], gain[d + 1]);
  }
  return gain[0];
}

void Position::makeMove(const Move& move, Undo& undo) {
  undo.move = move;
  undo.epSquare = epSquare_;
  undo.castlingRights = castling_;
  undo.halfmoveClock = halfmove_;
  undo.hash = hash_;

  const Piece moving = pieceOn(move.from);
  undo.captured = Piece::None;
  undo.capturedType = PieceType::None;

  epSquare_ = Square::None;

  if (move.flag == MoveFlag::EnPassant) {
    const int capSq =
        static_cast<int>(move.to) + (stm_ == Color::White ? -8 : 8);
    undo.captured = pieceOn(static_cast<Square>(capSq));
    undo.capturedType = PieceType::Pawn;
    xorPiece(static_cast<Square>(capSq), undo.captured);
    hash_ ^= zobrist::piece[capSq][static_cast<int>(undo.captured) - 1];
  } else if (move.flag == MoveFlag::Capture ||
             move.flag == MoveFlag::PromotionCapture) {
    undo.captured = pieceOn(move.to);
    undo.capturedType = pieceType(undo.captured);
    xorPiece(move.to, undo.captured);
    hash_ ^= zobrist::piece[static_cast<int>(move.to)]
                       [static_cast<int>(undo.captured) - 1];
  }

  xorPiece(move.from, moving);
  hash_ ^= zobrist::piece[static_cast<int>(move.from)][static_cast<int>(moving) - 1];

  PieceType placedType = pieceType(moving);
  if (move.promotion != PieceType::None) placedType = move.promotion;

  if (move.flag == MoveFlag::Castle) {
    const bool kingSide = fileOf(move.to) > fileOf(move.from);
    const Square rookFrom =
        kingSide ? squareFrom(7, rankOf(move.from)) : squareFrom(0, rankOf(move.from));
    const Square rookTo =
        kingSide ? squareFrom(5, rankOf(move.from)) : squareFrom(3, rankOf(move.from));
    const Piece rook = pieceOn(rookFrom);
    xorPiece(rookFrom, rook);
    xorPiece(rookTo, rook);
    hash_ ^= zobrist::piece[static_cast<int>(rookFrom)][static_cast<int>(rook) - 1];
    hash_ ^= zobrist::piece[static_cast<int>(rookTo)][static_cast<int>(rook) - 1];
  }

  const Piece placed = makePiece(stm_, placedType);
  xorPiece(move.to, placed);
  hash_ ^= zobrist::piece[static_cast<int>(move.to)][static_cast<int>(placed) - 1];

  if (pieceType(moving) == PieceType::Pawn || undo.captured != Piece::None) {
    halfmove_ = 0;
  } else {
    ++halfmove_;
  }

  if (pieceType(moving) == PieceType::Pawn &&
      (move.flag == MoveFlag::DoublePawnPush)) {
    epSquare_ = static_cast<Square>(static_cast<int>(move.from) +
                                  (stm_ == Color::White ? 8 : -8));
  }

  if (moving == Piece::WKing) castling_ &= ~(kCastleWK | kCastleWQ);
  if (moving == Piece::BKing) castling_ &= ~(kCastleBK | kCastleBQ);
  if (move.from == Square::H1 || move.to == Square::H1) castling_ &= ~kCastleWK;
  if (move.from == Square::A1 || move.to == Square::A1) castling_ &= ~kCastleWQ;
  if (move.from == Square::H8 || move.to == Square::H8) castling_ &= ~kCastleBK;
  if (move.from == Square::A8 || move.to == Square::A8) castling_ &= ~kCastleBQ;

  if (stm_ == Color::Black) ++fullmove_;
  stm_ = opposite(stm_);
  hash_ ^= zobrist::side;
  hash_ ^= zobrist::castling[castling_ & 15];
  if (undo.epSquare != Square::None) {
    hash_ ^= zobrist::epFile[fileOf(undo.epSquare)];
  }
  if (epSquare_ != Square::None) {
    hash_ ^= zobrist::epFile[fileOf(epSquare_)];
  }
}

void Position::unmakeMove(const Move& move, const Undo& undo) {
  stm_ = opposite(stm_);
  if (stm_ == Color::Black) --fullmove_;

  hash_ = undo.hash;
  castling_ = undo.castlingRights;
  epSquare_ = undo.epSquare;
  halfmove_ = undo.halfmoveClock;

  const Piece moving = pieceOn(move.to);
  xorPiece(move.to, moving);
  const Piece restored = makePiece(stm_, pieceType(moving));
  if (move.promotion != PieceType::None) {
    xorPiece(move.from, makePiece(stm_, PieceType::Pawn));
  } else {
    xorPiece(move.from, restored);
  }

  if (move.flag == MoveFlag::Castle) {
    const bool kingSide = fileOf(move.to) > fileOf(move.from);
    const Square rookFrom =
        kingSide ? squareFrom(7, rankOf(move.from)) : squareFrom(0, rankOf(move.from));
    const Square rookTo =
        kingSide ? squareFrom(5, rankOf(move.from)) : squareFrom(3, rankOf(move.from));
    const Piece rook = pieceOn(rookTo);
    xorPiece(rookTo, rook);
    xorPiece(rookFrom, rook);
  }

  if (undo.captured != Piece::None) {
    if (move.flag == MoveFlag::EnPassant) {
      const int capSq =
          static_cast<int>(move.to) + (stm_ == Color::White ? -8 : 8);
      xorPiece(static_cast<Square>(capSq), undo.captured);
    } else {
      xorPiece(move.to, undo.captured);
    }
  }
}

}  // namespace chess
