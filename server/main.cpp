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

}  // namespace

int main() {
  httplib::Server svr;

  svr.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
    setCors(res);
    res.status = 204;
  });

  svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    setCors(res);
    res.set_content(R"({"ok":true})", "application/json");
  });

  svr.Post("/api/bestmove", [](const httplib::Request& req, httplib::Response& res) {
    setCors(res);
    try {
      const auto body = nlohmann::json::parse(req.body);
      const std::string fen = body.at("fen").get<std::string>();
      chess::SearchLimits limits;
      limits.timeMs = body.value("timeMs", 2000);
      limits.maxDepth = body.value("maxDepth", 12);

      chess::Position probe;
      if (!probe.setFromFen(fen)) {
        res.status = 400;
        res.set_content(R"({"error":"invalid fen"})", "application/json");
        return;
      }

      const chess::SearchResult result = chess::findBestMove(fen, limits);
      if (result.bestMoveUci.empty()) {
        res.status = 400;
        res.set_content(R"({"error":"no legal moves"})", "application/json");
        return;
      }

      nlohmann::json out = {{"bestmove", result.bestMoveUci},
                            {"scoreCp", result.scoreCp},
                            {"depth", result.depthReached}};
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
