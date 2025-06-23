// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved

#define XDP_CORE_SOURCE
#include "parser_utils.h"

namespace xdp {
  bool jsonContainsRange(const boost::property_tree::ptree& jsonObj)
  {
    try {
      if (jsonObj.get_child_optional("start") == boost::none) {
        return false;
      }
      auto range = jsonObj.get_child_optional("start") ? parseArray(jsonObj.get_child("start")) : std::vector<uint8_t>{};
      if (range.empty()) {
        return false;
      }
      // NOTE: No need to check for "end" as it is optional and be same as start if not provided.
      return true;
    } catch (const boost::property_tree::ptree_error& e) {
      return false;
    }
  }
  
  bool jsonContainsAllRange(const boost::property_tree::ptree& jsonObj)
  {
    try {
      auto startValue = jsonObj.get_optional<std::string>("col");
      if (startValue && *startValue == "all") {
        return true;
      }
      startValue = jsonObj.get_optional<std::string>("row");
      if (startValue && *startValue == "all") {
        return true;
      }
      return false;
    } catch (const boost::property_tree::ptree_error& e) {
      return false;
    }
  }

}