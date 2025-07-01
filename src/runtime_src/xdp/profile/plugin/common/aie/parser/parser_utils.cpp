// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved

#define XDP_CORE_SOURCE
#include "parser_utils.h"

namespace xdp {
  bool jsonContainsRange(MetricType metricType, const boost::property_tree::ptree& jsonObj)
  {
    if ((metricType >= MetricType::TILE_BASED_AIE_TILE) ||
        (metricType < MetricType::GRAPH_BASED_AIE_TILE))
    {
      try {
        if (jsonObj.get_child_optional("start") == boost::none)
          return false;

        auto range = jsonObj.get_child_optional("start") ?
                        parseArray(jsonObj.get_child("start")) : std::vector<uint8_t>{};
        if (range.empty())
          return false;

        // NOTE: No need to check for "end" as it is optional and be same as start if not provided.
        return true;
      } catch (const boost::property_tree::ptree_error& e) {
        return false;
      }
    }
  }
  
  bool jsonContainsAllRange(MetricType metricType, const boost::property_tree::ptree& jsonObj)
  {
    if ((metricType >= MetricType::TILE_BASED_AIE_TILE) ||
        (metricType < MetricType::GRAPH_BASED_AIE_TILE))
    {
      try {
        auto allTiles = jsonObj.get_optional<bool>("all_tiles");
        if (allTiles && *allTiles)
          return true;
      } catch (const boost::property_tree::ptree_error& e) {
        return false;
      }
      return false;
    }
    else if (metricType >= MetricType::GRAPH_BASED_AIE_TILE &&
             metricType <= MetricType::GRAPH_BASED_MEM_TILE)
    {
      try {
        auto allGraphs = jsonObj.get_optional<bool>("all_graphs");
        if (allGraphs && *allGraphs)
          return true;
      } catch (const boost::property_tree::ptree_error& e) {
        return false;
      }
      return false;
    }
  }
}