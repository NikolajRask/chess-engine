#pragma once

#include "chess/search.hpp"
#include "chess/types.hpp"

namespace chess {

struct TimeAllocation {
  int softMs = 0;  // prefer finishing current depth after this
  int hardMs = 0;  // must stop
};

// Compute soft/hard budgets from SearchLimits and side to move.
TimeAllocation allocateTime(const SearchLimits& limits, Color stm);

// Clamp skill 0–20 and adjust depth/nodes on limits (mutates maxDepth/nodes).
void applySkillLimits(SearchLimits& limits);

// How many top root moves to consider when skill < 20.
int skillCandidateCount(int skillLevel);

}  // namespace chess
