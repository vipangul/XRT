// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved
#ifndef XDP_JSON_PARSER_H
#define XDP_JSON_PARSER_H

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <string>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include "core/common/message.h"
#include "metrics_collection.h"
#include "xdp/profile/database/static_info/aie_constructs.h"

namespace xdp {
namespace pt = boost::property_tree;
using severity_level = xrt_core::message::severity_level;

// JsonParser for reading and writing JSON files
class JsonParser {
  private:
  // public:
    // Data structure to store MetricCollection objects for different plugin types
    // static std::map<std::string, MetricCollection> pluginMetricCollections;
    // static std::map<module_type, std::map<std::string, MetricCollection>> allModulesMetricCollections;
    std::map<module_type, std::map<std::string, MetricCollection>> allModulesMetricCollections;
  public:
    // static MetricCollection& parse(const std::string& jsonFilePath) {
    void parse(const std::string& jsonFilePath);
    static void write(const std::string& filename, const MetricCollection& collection);
    const MetricCollection& getMetricCollection(module_type mod, const std::string& settingName);
    
    };

// Define the static member
// std::map<std::string, MetricCollection> JsonParser::pluginMetricCollections;
// std::map<module_type, std::map<std::string, MetricCollection>> JsonParser::allModulesMetricCollections;
};

#endif