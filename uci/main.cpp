#include "chess/book.hpp"
#include "chess/engine.hpp"
#include "chess/game.hpp"
#include "chess/movegen.hpp"
#include "chess/nnue.hpp"
#include "chess/search.hpp"

#include <algorithm>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct UciState {
  chess::Position pos;
  std::vector<chess::Bitboard> hashHistory;
  chess::Searcher& searcher = chess::globalSearcher();
  int threads = 1;
  int skill = 20;
  bool ownBook = true;
  std::string bookFile;
  size_t hashMb = 16;
  bool useNnue = false;
  std::string evalFile = "nets/default.nnue";
};

void resetPosition(UciState& st, const std::string& fen) {
  st.pos.setFromFen(fen);
  st.hashHistory.clear();
  st.hashHistory.push_back(st.pos.hash());
}

void applyMoves(UciState& st, const std::vector<std::string>& moves) {
  st.hashHistory = chess::applyUciMoves(st.pos, moves);
}

void cmdUci(UciState&) {
  std::cout << "id name ChessAI\n";
  std::cout << "id author ChessAI\n";
  std::cout << "option name Threads type spin default 1 min 1 max 64\n";
  std::cout << "option name Hash type spin default 16 min 1 max 1024\n";
  std::cout << "option name Skill Level type spin default 20 min 0 max 20\n";
  std::cout << "option name OwnBook type check default true\n";
  std::cout << "option name BookFile type string default\n";
  std::cout << "option name UseNNUE type check default false\n";
  std::cout << "option name EvalFile type string default nets/default.nnue\n";
  std::cout << "uciok" << std::endl;
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
  } else if (name == "UseNNUE") {
    st.useNnue = (value == "true" || value == "1");
    chess::nnue::setUseNnue(st.useNnue);
  } else if (name == "EvalFile") {
    st.evalFile = value;
    if (st.evalFile.empty()) {
      chess::nnue::unload();
    } else if (chess::nnue::load(st.evalFile)) {
      st.pos.refreshNnue();
      chess::nnue::setUseNnue(st.useNnue);
    } else {
      std::cerr << "info string failed to load EvalFile " << st.evalFile << std::endl;
    }
  }
}

void cmdBench(UciState& st) {
  static const char* kFens[] = {
      "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
      "r3k2r/p1ppqpb1/bn2pnp1/3PP3/2P2P1q/2N2B1P/1P2PBP1/R2QK2R w KQkq - 0 1",
  };
  constexpr int kDepth = 10;
  chess::SearchLimits lim;
  lim.threads = 1;
  lim.skillLevel = 20;
  lim.useBook = false;
  lim.hashMb = st.hashMb;
  lim.maxDepth = kDepth;
  lim.timeMs = 0;
  lim.infinite = false;
  lim.movetime = 0;
  lim.wtime = 0;
  lim.btime = 0;

  std::uint64_t totalNodes = 0;
  int totalMs = 0;
  st.searcher.setInfoCallback({});
  for (const char* fen : kFens) {
    st.pos.setFromFen(fen);
    st.searcher.newGame();
    const chess::SearchResult r = st.searcher.search(st.pos, lim);
    totalNodes += r.nodes;
    totalMs += r.timeMs;
    std::cout << "info string bench fen nodes " << r.nodes << " time " << r.timeMs
              << " nps " << r.nps << " depth " << r.depthReached << std::endl;
  }
  const std::uint64_t nps =
      totalMs > 0 ? (totalNodes * 1000ull) / static_cast<std::uint64_t>(totalMs) : 0;
  std::cout << "info string bench total nodes " << totalNodes << " time " << totalMs
            << " nps " << nps << std::endl;
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

  const chess::SearchResult result = st.searcher.search(st.pos, lim, st.hashHistory);
  if (result.gameOver) {
    std::cout << "info string " << result.outcome << std::endl;
  } else if (result.fromBook) {
    std::cout << "info string book move" << std::endl;
  } else if (!result.bestMoveUci.empty()) {
    std::cout << "info depth " << result.depthReached;
    if (result.scoreMate.has_value()) {
      // UCI mate is in moves; convert from signed plies (ceil).
      const int plies = *result.scoreMate;
      const int moves = (std::abs(plies) + 1) / 2;
      std::cout << " score mate " << (plies > 0 ? moves : -moves);
    } else {
      std::cout << " score cp " << result.scoreCp;
    }
    std::cout << " nodes " << result.nodes << " time " << result.timeMs << " nps " << result.nps;
    if (!result.pv.empty()) {
      std::cout << " pv";
      for (const auto& m : result.pv) std::cout << ' ' << m;
    }
    std::cout << std::endl;
  }
  std::cout << "bestmove " << (result.bestMoveUci.empty() ? "0000" : result.bestMoveUci)
            << std::endl;
}

}  // namespace

int main() {
  std::ios::sync_with_stdio(false);
  UciState st;
  if (chess::nnue::load(st.evalFile) || chess::nnue::loadDefault()) {
    chess::nnue::setUseNnue(st.useNnue);
  } else {
    chess::nnue::setUseNnue(false);
  }
  resetPosition(st, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
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
      std::cout << "readyok" << std::endl;
    } else if (cmd == "ucinewgame") {
      st.searcher.newGame();
      resetPosition(st, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    } else if (cmd == "position") {
      std::string type;
      iss >> type;
      std::vector<std::string> moves;
      if (type == "startpos") {
        resetPosition(st, "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
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
        resetPosition(st, fen);
        std::string maybeMoves;
        if (iss >> maybeMoves && maybeMoves == "moves") {
          std::string m;
          while (iss >> m) moves.push_back(m);
        }
      }
      if (!moves.empty()) applyMoves(st, moves);
    } else if (cmd == "go") {
      cmdGo(st, iss);
    } else if (cmd == "bench") {
      cmdBench(st);
    } else if (cmd == "setoption") {
      cmdSetOption(st, iss);
    } else if (cmd == "stop") {
    } else if (cmd == "quit") {
      break;
    }
  }
  return 0;
}
