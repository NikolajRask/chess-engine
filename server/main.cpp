#include "chess/engine.hpp"
#include "chess/game.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace {

void setCors(const httplib::Request& req, httplib::Response& res) {
  const std::string origin = req.get_header_value("Origin");
  if (origin == "http://localhost:5173" || origin == "http://127.0.0.1:5173") {
    res.set_header("Access-Control-Allow-Origin", origin);
  } else {
    res.set_header("Access-Control-Allow-Origin", "http://localhost:5173");
  }
  res.set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type");
}

chess::SearchLimits limitsFromJson(const nlohmann::json& body) {
  chess::SearchLimits limits;
  limits.timeMs = body.value("timeMs", 2000);
  limits.movetime = body.value("movetime", 0);
  limits.maxDepth = body.value("maxDepth", 64);
  limits.threads = body.value("threads", 1);
  limits.skillLevel = body.value("skillLevel", 20);
  limits.multiPv = body.value("multiPv", 1);
  limits.useBook = body.value("useBook", true);
  limits.wtime = body.value("wtime", 0);
  limits.btime = body.value("btime", 0);
  limits.winc = body.value("winc", 0);
  limits.binc = body.value("binc", 0);
  limits.movestogo = body.value("movestogo", 0);
  if (body.contains("nodes")) limits.nodes = body["nodes"].get<std::uint64_t>();
  return limits;
}

nlohmann::json resultToJson(const chess::SearchResult& result) {
  nlohmann::json candidates = nlohmann::json::array();
  if (!result.multipv.empty()) {
    for (const auto& line : result.multipv) {
      candidates.push_back(
          {{"bestmove", line.moveUci},
           {"scoreCp", line.scoreCp},
           {"scoreMate", line.scoreMate.has_value() ? nlohmann::json(*line.scoreMate)
                                                   : nlohmann::json(nullptr)},
           {"pv", line.pv}});
    }
  } else if (!result.bestMoveUci.empty()) {
    candidates.push_back({{"bestmove", result.bestMoveUci},
                          {"scoreCp", result.scoreCp},
                          {"scoreMate", result.scoreMate.has_value()
                                            ? nlohmann::json(*result.scoreMate)
                                            : nlohmann::json(nullptr)},
                          {"pv", result.pv}});
  }

  return {{"bestmove", result.bestMoveUci},
          {"scoreCp", result.scoreCp},
          {"scoreMate", result.scoreMate.has_value() ? nlohmann::json(*result.scoreMate)
                                                    : nlohmann::json(nullptr)},
          {"depth", result.depthReached},
          {"nodes", result.nodes},
          {"timeMs", result.timeMs},
          {"nps", result.nps},
          {"pv", result.pv},
          {"fromBook", result.fromBook},
          {"gameOver", result.gameOver},
          {"outcome", result.outcome},
          {"candidates", candidates}};
}

}  // namespace

int main() {
  httplib::Server svr;
  chess::Searcher& searcher = chess::globalSearcher();

  svr.Options(R"(/.*)", [](const httplib::Request& req, httplib::Response& res) {
    setCors(req, res);
    res.status = 204;
  });

  svr.Get("/health", [](const httplib::Request& req, httplib::Response& res) {
    setCors(req, res);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Post("/api/bestmove", [&searcher](const httplib::Request& req, httplib::Response& res) {
    setCors(req, res);
    try {
      const auto body = nlohmann::json::parse(req.body);
      const std::string fen = body.at("fen").get<std::string>();
      chess::SearchLimits limits = limitsFromJson(body);

      chess::Position pos;
      if (!pos.setFromFen(fen)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid fen"})", "application/json");
        return;
      }

      std::vector<std::string> moves;
      if (body.contains("moves") && body["moves"].is_array()) {
        for (const auto& m : body["moves"]) {
          moves.push_back(m.get<std::string>());
        }
      }

      // Prefer startFen + moves when provided so repetition history is complete.
      std::vector<chess::Bitboard> history;
      if (body.contains("startFen") && body["startFen"].is_string()) {
        const std::string startFen = body["startFen"].get<std::string>();
        if (!pos.setFromFen(startFen)) {
          res.status = 400;
          res.set_content(R"({"error":"invalid startFen"})", "application/json");
          return;
        }
        history = chess::applyUciMoves(pos, moves);
      } else if (!moves.empty()) {
        // Moves relative to the given fen (fen is the starting position of the move list).
        history = chess::applyUciMoves(pos, moves);
      } else {
        history.push_back(pos.hash());
      }

      const chess::SearchResult result = searcher.search(pos, limits, history);
      if (result.gameOver) {
        res.set_content(resultToJson(result).dump(), "application/json");
        return;
      }
      if (result.bestMoveUci.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"no legal moves"})", "application/json");
        return;
      }

      res.set_content(resultToJson(result).dump(), "application/json");
    } catch (const std::exception& e) {
      res.status = 400;
      nlohmann::json err = {{"error", e.what()}};
      res.set_content(err.dump(), "application/json");
    }
  });

  const int port = 8080;
  std::cout << "Chess server listening on http://localhost:" << port << std::endl;
  svr.listen("0.0.0.0", port);
  return 0;
}
