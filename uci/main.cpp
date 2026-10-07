#include "chess/book.hpp"
#include "chess/engine.hpp"
#include "chess/movegen.hpp"
#include "chess/search.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct UciState {
  chess::Position pos;
  chess::Searcher& searcher = chess::globalSearcher();
  int threads = 1;
  int skill = 20;
  bool ownBook = true;
  std::string bookFile;
  size_t hashMb = 16;
};

void applyMoves(chess::Position& pos, const std::vector<std::string>& moves) {
  for (const std::string& u : moves) {
    auto m = chess::legalMoveFromUci(pos, u);
    if (!m) break;
    chess::Undo undo;
    pos.makeMove(*m, undo);
  }
}

void cmdUci(UciState&) {
  std::cout << "id name ChessAI\n";
  std::cout << "id author ChessAI\n";
  std::cout << "option name Threads type spin default 1 min 1 max 64\n";
  std::cout << "option name Hash type spin default 16 min 1 max 1024\n";
  std::cout << "option name Skill Level type spin default 20 min 0 max 20\n";
  std::cout << "option name OwnBook type check default true\n";
  std::cout << "option name BookFile type string default\n";
  std::cout << "uciok\n";
}

void cmdSetOption(UciState& st, std::istringstream& iss) {
  std::string token;
  iss >> token;  // name
  std::string name;
  while (iss >> token && token != "value") {
    if (!name.empty()) name.push_back(' ');
    name += token;
  }
  std::string value;
  std::getline(iss, value);
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
  while (!value.empty() &&
         (value.back() == ' ' || value.back() == '\t' || value.back() == '\r')) {
    value.pop_back();
  }

  if (name == "Threads") {
    st.threads = std::max(1, std::stoi(value));
  } else if (name == "Hash") {
    st.hashMb = static_cast<size_t>(std::max(1, std::stoi(value)));
    st.searcher.resizeHash(st.hashMb);
  } else if (name == "Skill Level") {
    st.skill = std::clamp(std::stoi(value), 0, 20);
  } else if (name == "OwnBook") {
    st.ownBook = (value == "true" || value == "1");
  } else if (name == "BookFile") {
    st.bookFile = value;
    if (!st.bookFile.empty()) {
      chess::globalBook().loadPolyglot(st.bookFile);
    } else {
      chess::globalBook().clearExternal();
    }
  }
}

void cmdGo(UciState& st, std::istringstream& iss) {
  chess::SearchLimits lim;
  lim.threads = st.threads;
  lim.skillLevel = st.skill;
  lim.useBook = st.ownBook;
  lim.hashMb = st.hashMb;
  lim.timeMs = 0;
  lim.maxDepth = 64;

  std::string token;
  while (iss >> token) {
    if (token == "depth") {
      iss >> lim.maxDepth;
    } else if (token == "nodes") {
      std::uint64_t n = 0;
      iss >> n;
      lim.nodes = n;
    } else if (token == "movetime") {
      iss >> lim.movetime;
    } else if (token == "wtime") {
      iss >> lim.wtime;
    } else if (token == "btime") {
      iss >> lim.btime;
    } else if (token == "winc") {
      iss >> lim.winc;
    } else if (token == "binc") {
      iss >> lim.binc;
    } else if (token == "movestogo") {
      iss >> lim.movestogo;
    } else if (token == "infinite") {
      lim.infinite = true;
    }
  }

  st.searcher.setInfoCallback([](int depth, int score, std::uint64_t nodes) {
    std::cout << "info depth " << depth << " score cp " << score << " nodes " << nodes
              << std::endl;
  });

  const chess::SearchResult result = st.searcher.search(st.pos, lim);
  if (result.fromBook) {
    std::cout << "info string book move\n";
  }
  std::cout << "bestmove " << (result.bestMoveUci.empty() ? "0000" : result.bestMoveUci)
            << std::endl;
}

}  // namespace

int main() {
  std::ios::sync_with_stdio(false);
  UciState st;
  st.pos.setFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  st.searcher.resizeHash(st.hashMb);

  std::string line;
  while (std::getline(std::cin, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;

    if (cmd == "uci") {
      cmdUci(st);
    } else if (cmd == "isready") {
      std::cout << "readyok\n";
    } else if (cmd == "ucinewgame") {
      st.searcher.newGame();
      st.pos.setFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    } else if (cmd == "position") {
      std::string type;
      iss >> type;
      std::vector<std::string> moves;
      if (type == "startpos") {
        st.pos.setFromFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
        std::string maybeMoves;
        if (iss >> maybeMoves && maybeMoves == "moves") {
          std::string m;
          while (iss >> m) moves.push_back(m);
        }
      } else if (type == "fen") {
        std::string fen;
        std::string part;
        for (int i = 0; i < 6 && iss >> part; ++i) {
          if (i) fen.push_back(' ');
          fen += part;
        }
        st.pos.setFromFen(fen);
        std::string maybeMoves;
        if (iss >> maybeMoves && maybeMoves == "moves") {
          std::string m;
          while (iss >> m) moves.push_back(m);
        }
      }
      applyMoves(st.pos, moves);
    } else if (cmd == "go") {
      cmdGo(st, iss);
    } else if (cmd == "setoption") {
      cmdSetOption(st, iss);
    } else if (cmd == "stop") {
    } else if (cmd == "quit") {
      break;
    }
  }
  return 0;
}
