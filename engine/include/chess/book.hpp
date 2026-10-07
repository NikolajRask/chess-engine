#pragma once

#include "chess/position.hpp"
#include "chess/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace chess {

struct BookEntry {
  Move move{};
  int weight = 1;
};

class Book {
 public:
  Book();

  // Load optional Polyglot .bin (adds/overrides entries). Returns false on I/O error.
  bool loadPolyglot(const std::string& path);

  // Weighted random legal book move, or nullopt.
  std::optional<Move> probe(const Position& pos) const;

  void clearExternal();
  bool hasBuiltin() const { return !builtin_.empty(); }

 private:
  void buildBuiltin();
  void addLine(const std::vector<std::string>& uciMoves);
  static std::uint64_t polyglotKey(const Position& pos);

  std::unordered_map<Bitboard, std::vector<BookEntry>> builtin_;
  std::unordered_map<std::uint64_t, std::vector<BookEntry>> polyglot_;
  bool usePolyglot_ = false;
};

Book& globalBook();

}  // namespace chess
