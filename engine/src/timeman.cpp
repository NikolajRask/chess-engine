#include "chess/timeman.hpp"

#include <algorithm>

namespace chess {

TimeAllocation allocateTime(const SearchLimits& limits, Color stm) {
  TimeAllocation alloc;

  if (limits.infinite) {
    alloc.softMs = 0;
    alloc.hardMs = 0;
    return alloc;
  }

  if (limits.movetime > 0) {
    alloc.hardMs = limits.movetime;
    alloc.softMs = std::max(1, (limits.movetime * 80) / 100);
    return alloc;
  }

  const int remaining = stm == Color::White ? limits.wtime : limits.btime;
  const int inc = stm == Color::White ? limits.winc : limits.binc;
  if (remaining > 0) {
    const int mtg = limits.movestogo > 0 ? limits.movestogo : 30;
    int target = remaining / std::max(1, mtg) + inc / 2;
    target = std::max(10, target);
    // Don't burn more than ~20% of remaining on one move (plus a small share of incr).
    const int cap = remaining / 5 + inc;
    target = std::min(target, std::max(10, cap));
    alloc.hardMs = std::min(remaining - 15, target + target / 4);
    alloc.hardMs = std::max(10, alloc.hardMs);
    alloc.softMs = std::max(1, (alloc.hardMs * 80) / 100);
    return alloc;
  }

  // HTTP / legacy fixed budget.
  if (limits.timeMs > 0) {
    alloc.hardMs = limits.timeMs;
    alloc.softMs = std::max(1, (limits.timeMs * 80) / 100);
  }
  return alloc;
}

void applySkillLimits(SearchLimits& limits) {
  int skill = limits.skillLevel;
  if (skill < 0) skill = 0;
  if (skill > 20) skill = 20;
  limits.skillLevel = skill;

  if (skill >= 20) return;

  int depthCap = 64;
  std::uint64_t nodeCap = 0;
  if (skill <= 5) {
    depthCap = 2 + skill / 2;  // 2–4
    nodeCap = 2000ull + static_cast<std::uint64_t>(skill) * 3000ull;
  } else if (skill <= 12) {
    depthCap = 5 + (skill - 6) / 2;  // 5–8
    nodeCap = 20000ull + static_cast<std::uint64_t>(skill - 6) * 15000ull;
  } else {
    depthCap = 10 + (skill - 13);  // 10–16
    if (depthCap > 14) depthCap = 14;
    nodeCap = 150000ull + static_cast<std::uint64_t>(skill - 13) * 80000ull;
  }

  if (limits.maxDepth <= 0 || limits.maxDepth > depthCap) limits.maxDepth = depthCap;
  if (limits.nodes == 0 || limits.nodes > nodeCap) limits.nodes = nodeCap;
}

int skillCandidateCount(int skillLevel) {
  if (skillLevel >= 20) return 1;
  if (skillLevel <= 5) return 5;
  if (skillLevel <= 12) return 3;
  return 2;
}

}  // namespace chess
