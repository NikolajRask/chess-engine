#include "chess/book.hpp"

#include "chess/movegen.hpp"

#include <cstdint>
#include <fstream>
#include <random>

namespace chess {

namespace {

// Standard Polyglot random piece keys (subset used for hashing).
// Full 781-key table from the Polyglot book format.
#include "polyglot_keys.inc"

std::optional<Move> matchLegal(Position& pos, Square from, Square to, PieceType promo) {
  std::vector<Move> moves;
  MoveGen::generateLegal(pos, moves);
  for (const Move& m : moves) {
    if (m.from == from && m.to == to && m.promotion == promo) return m;
  }
  return std::nullopt;
}

PieceType promoFromPolyglot(int promoCode) {
  // Polyglot: 0 none, 1 knight, 2 bishop, 3 rook, 4 queen
  switch (promoCode) {
    case 1:
      return PieceType::Knight;
    case 2:
      return PieceType::Bishop;
    case 3:
      return PieceType::Rook;
    case 4:
      return PieceType::Queen;
    default:
      return PieceType::None;
  }
}

}  // namespace

Book::Book() { buildBuiltin(); }

void Book::addLine(const std::vector<std::string>& uciMoves) {
  Position pos;
  for (const std::string& u : uciMoves) {
    auto parsed = uciToMove(u);
    if (!parsed) break;
    auto legal = matchLegal(pos, parsed->from, parsed->to, parsed->promotion);
    if (!legal) break;
    builtin_[pos.hash()].push_back(BookEntry{*legal, 1});
    Undo undo;
    pos.makeMove(*legal, undo);
  }
}

void Book::buildBuiltin() {
  builtin_.clear();
  // Common opening trees (~8–12 plies).
  addLine({"e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6", "b5a4", "g8f6"});
  addLine({"e2e4", "e7e5", "g1f3", "b8c6", "f1c4", "g8f6", "d2d3", "f8c5"});
  addLine({"e2e4", "e7e5", "g1f3", "g8f6", "f3e5", "d7d6", "e5f3", "f6e4"});
  addLine({"e2e4", "c7c5", "g1f3", "d7d6", "d2d4", "c5d4", "f3d4", "g8f6"});
  addLine({"e2e4", "c7c5", "g1f3", "b8c6", "d2d4", "c5d4", "f3d4", "g8f6"});
  addLine({"e2e4", "e7e6", "d2d4", "d7d5", "b1c3", "g8f6", "e4e5", "f6d7"});
  addLine({"e2e4", "c7c6", "d2d4", "d7d5", "b1c3", "d5e4", "c3e4", "c8f5"});
  addLine({"d2d4", "d7d5", "c2c4", "e7e6", "b1c3", "g8f6", "c4d5", "e6d5"});
  addLine({"d2d4", "d7d5", "c2c4", "c7c6", "g1f3", "g8f6", "b1c3", "e7e6"});
  addLine({"d2d4", "g8f6", "c2c4", "g7g6", "b1c3", "f8g7", "e2e4", "d7d6"});
  addLine({"d2d4", "g8f6", "c2c4", "e7e6", "g1f3", "d7d5", "b1c3", "f8e7"});
  addLine({"c2c4", "e7e5", "b1c3", "g8f6", "g1f3", "b8c6", "g2g3", "d7d5"});
  addLine({"g1f3", "d7d5", "d2d4", "g8f6", "c2c4", "e7e6", "b1c3", "f8e7"});
  addLine({"e2e4", "e7e5", "f2f4", "e5f4", "g1f3", "g7g5", "f1c4", "g5g4"});
  // Also weight popular first moves from the start position.
  Position start;
  for (const char* u : {"e2e4", "d2d4", "c2c4", "g1f3"}) {
    auto parsed = uciToMove(u);
    if (!parsed) continue;
    auto legal = matchLegal(start, parsed->from, parsed->to, parsed->promotion);
    if (legal) {
      int w = (u[0] == 'e') ? 40 : (u[0] == 'd' ? 35 : 15);
      builtin_[start.hash()].push_back(BookEntry{*legal, w});
    }
  }
}

std::uint64_t Book::polyglotKey(const Position& pos) {
  std::uint64_t key = 0;
  // Polyglot piece encoding: kind*2+color, kind: pawn=0..king=5, white=0 black=1
  // Square: file + 8*rank (a1=0).
  for (int sq = 0; sq < 64; ++sq) {
    const Piece p = pos.pieceOn(static_cast<Square>(sq));
    if (p == Piece::None) continue;
    const PieceType pt = pieceType(p);
    const Color c = pieceColor(p);
    const int kind = static_cast<int>(pt) - 1;  // 0..5
    const int color = c == Color::White ? 0 : 1;
    const int pieceIdx = kind * 2 + color;
    const int pgSq = fileOf(static_cast<Square>(sq)) + 8 * rankOf(static_cast<Square>(sq));
    key ^= kPolyglotRandom[64 * pieceIdx + pgSq];
  }

  const int cr = pos.castlingRights();
  if (cr & 1) key ^= kPolyglotRandom[768];  // WK
  if (cr & 2) key ^= kPolyglotRandom[769];  // WQ
  if (cr & 4) key ^= kPolyglotRandom[770];  // BK
  if (cr & 8) key ^= kPolyglotRandom[771];  // BQ

  if (pos.epSquare() != Square::None) {
    // Polyglot only hashes ep if a legal en passant capture exists.
    const Color us = pos.sideToMove();
    const Square ep = pos.epSquare();
    const int epFile = fileOf(ep);
    bool legalEp = false;
    for (int df : {-1, 1}) {
      const int f = epFile + df;
      if (f < 0 || f > 7) continue;
      const int pawnRank = us == Color::White ? rankOf(ep) - 1 : rankOf(ep) + 1;
      if (pawnRank < 0 || pawnRank > 7) continue;
      if (pos.pieceOn(squareFrom(f, pawnRank)) == makePiece(us, PieceType::Pawn)) {
        legalEp = true;
        break;
      }
    }
    if (legalEp) key ^= kPolyglotRandom[772 + epFile];
  }

  if (pos.sideToMove() == Color::White) key ^= kPolyglotRandom[780];
  return key;
}

bool Book::loadPolyglot(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  polyglot_.clear();
  usePolyglot_ = true;

  auto readBe16 = [&]() -> std::uint16_t {
    unsigned char b[2];
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<std::uint16_t>((b[0] << 8) | b[1]);
  };
  auto readBe32 = [&]() -> std::uint32_t {
    unsigned char b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    return (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16) |
           (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
  };
  auto readBe64 = [&]() -> std::uint64_t {
    unsigned char b[8];
    in.read(reinterpret_cast<char*>(b), 8);
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | b[i];
    return v;
  };

  while (in && in.peek() != EOF) {
    const std::uint64_t key = readBe64();
    const std::uint16_t move = readBe16();
    const std::uint16_t weight = readBe16();
    (void)readBe32();  // learn
    if (!in) break;

    const int toFile = move & 7;
    const int toRank = (move >> 3) & 7;
    const int fromFile = (move >> 6) & 7;
    const int fromRank = (move >> 9) & 7;
    const int promo = (move >> 12) & 7;
    Move m;
    m.from = squareFrom(fromFile, fromRank);
    m.to = squareFrom(toFile, toRank);
    m.promotion = promoFromPolyglot(promo);
    polyglot_[key].push_back(BookEntry{m, std::max(1, static_cast<int>(weight))});
  }
  return true;
}

void Book::clearExternal() {
  polyglot_.clear();
  usePolyglot_ = false;
}

std::optional<Move> Book::probe(const Position& pos) const {
  std::vector<BookEntry> candidates;

  if (usePolyglot_) {
    const auto it = polyglot_.find(polyglotKey(pos));
    if (it != polyglot_.end()) {
      Position copy = pos;
      for (const BookEntry& e : it->second) {
        auto legal = matchLegal(copy, e.move.from, e.move.to, e.move.promotion);
        if (legal) candidates.push_back(BookEntry{*legal, e.weight});
      }
    }
  }

  if (candidates.empty()) {
    const auto it = builtin_.find(pos.hash());
    if (it != builtin_.end()) {
      Position copy = pos;
      for (const BookEntry& e : it->second) {
        auto legal = matchLegal(copy, e.move.from, e.move.to, e.move.promotion);
        if (legal) candidates.push_back(BookEntry{*legal, e.weight});
      }
    }
  }

  if (candidates.empty()) return std::nullopt;

  int total = 0;
  for (const auto& c : candidates) total += c.weight;
  thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(1, std::max(1, total));
  int pick = dist(rng);
  for (const auto& c : candidates) {
    pick -= c.weight;
    if (pick <= 0) return c.move;
  }
  return candidates.back().move;
}

Book& globalBook() {
  static Book book;
  return book;
}

}  // namespace chess
