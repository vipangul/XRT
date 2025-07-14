// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved
#ifndef PARSER_UTILS_H
#define PARSER_UTILS_H

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <string>
#include <iostream> // TODO: Delete this after debugging
#include "core/common/config_reader.h"
#include "xdp/config.h"
#include "xdp/profile/plugin/parser/metrics_type.h"
#include "xdp/profile/database/static_info/aie_constructs.h"

namespace xdp {

  // Helper Functions
  inline std::vector<uint8_t> parseArray(const boost::property_tree::ptree& arrayNode) {
    std::vector<uint8_t> result;
    for (const auto& item : arrayNode) {
        result.push_back(static_cast<uint8_t>(item.second.get_value<int>()));
    }
    return result;
  }

  MetricType getMetricTypeFromKey(const std::string& settingsKey, const std::string& key);
  module_type getModuleTypeFromKey(const std::string& key);
  bool jsonContainsRange(MetricType metricType, const boost::property_tree::ptree& jsonObj);
  bool jsonContainsAllRange(MetricType metricType, const boost::property_tree::ptree& jsonObj);
  // bool parseXdpJson(SettingsJsonParser& jsonParser, boost::property_tree::ptree& jsonTree);

} // end namespace xdp
#endif