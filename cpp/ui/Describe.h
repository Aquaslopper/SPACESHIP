#pragma once

#include <string>
#include <vector>

#include "core/Simulation.h"

namespace ship {

struct DescribeRow {
  std::string key;
  std::string value;
};

struct DescribeAct {
  std::string label;
  std::string cost;
  bool live = false;
};

struct Description {
  std::string kind;
  std::string id;
  std::string kindLabel;
  std::string title;
  std::string sub;
  std::vector<DescribeRow> rows;
  std::vector<DescribeAct> acts;
  bool gone = false;
};

bool DescribeTargetExists(const Simulation& sim, const std::string& kind,
                          const std::string& id);

Description DescribeTarget(const Simulation& sim, const std::string& kind,
                           const std::string& id);

std::string JobThreshold(const Simulation& sim, const CrewMember& crew);

}  // namespace ship
