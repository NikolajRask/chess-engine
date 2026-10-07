#include "chess/engine.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <iostream>
#include <string>

namespace {

void setCors(httplib::Response& res) {
  res.set_header("Access-Control-Allow-Origin", "http://localhost:5173");
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
  limits.useBook = body.value("useBook", true);
  limits.wtime = body.value("wtime", 0);
  limits.btime = body.value("btime", 0);
  limits.winc = body.value("winc", 0);
  limits.binc = body.value("binc", 0);
  limits.movestogo = body.value("movestogo", 0);
  if (body.contains("nodes")) limits.nodes = body["nodes"].get<std::uint64_t>();
  return limits;
}

}  // namespace

int main() {
  httplib::Server svr;
  chess::Searcher& searcher = chess::globalSearcher();

  svr.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
    setCors(res);
    res.status = 204;
  });

  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    setCors(res);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Post("/api/bestmove", [&searcher](const httplib::Request& req, httplib::Response& res) {
    setCors(res);
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

      const chess::SearchResult result = searcher.search(pos, limits);
      if (result.bestMoveUci.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"no legal moves"})", "application/json");
        return;
      }

      nlohmann::json out = {{"bestmove", result.bestMoveUci},
                            {"scoreCp", result.scoreCp},
                            {"depth", result.depthReached},
                            {"fromBook", result.fromBook}};
      res.set_content(out.dump(), "application/json");
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
