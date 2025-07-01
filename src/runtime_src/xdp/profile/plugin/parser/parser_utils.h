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

namespace xdp {

  // Helper Functions
  inline std::vector<uint8_t> parseArray(const boost::property_tree::ptree& arrayNode) {
    std::vector<uint8_t> result;
    for (const auto& item : arrayNode) {
        result.push_back(static_cast<uint8_t>(item.second.get_value<int>()));
    }
    return result;
  }

  XDP_CORE_EXPORT bool jsonContainsRange(MetricType metricType, const boost::property_tree::ptree& jsonObj);
  XDP_CORE_EXPORT bool jsonContainsAllRange(MetricType metricType, const boost::property_tree::ptree& jsonObj);
  // XDP_CORE_EXPORT bool parseXdpJson(SettingsJsonParser& jsonParser, boost::property_tree::ptree& jsonTree);

} // end namespace xdp
#endif