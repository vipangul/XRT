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
#include <map>
#include "core/common/message.h"
#include "metrics_collection.h"
#include "xdp/profile/database/static_info/aie_constructs.h"

namespace xdp {
  namespace pt = boost::property_tree;
  using severity_level = xrt_core::message::severity_level;

  enum class PluginType {
      AIE_PROFILE,
      AIE_TRACE,
      UNKNOWN
  };

  struct PluginConfig {
      PluginType type;
      std::map<std::string, std::map<std::string, std::vector<pt::ptree>>> sections;
      bool isValid = false;
      std::string errorMessage;
  };

  struct XdpConfig {
      std::map<PluginType, PluginConfig> plugins;
      bool isValid = false;
      std::string errorMessage;
  };

  // JsonParser for reading and writing JSON files
  class JsonParser {
    private:
      JsonParser() = default;
      JsonParser(const JsonParser&) = delete;
      JsonParser& operator=(const JsonParser&) = delete;

      // Plugin-specific module mappings
      static const std::map<PluginType, std::vector<std::string>> PLUGIN_MODULES;
      static const std::map<PluginType, std::vector<std::string>> PLUGIN_SECTIONS;
      
      // bool validatePluginSchema(const pt::ptree& tree, PluginType pluginType);
      PluginType getPluginTypeFromString(const std::string& pluginName);
      
    public:
      static JsonParser& getInstance() {
        static JsonParser instance;
        return instance;
      }

      pt::ptree parse(const std::string& jsonFilePath);
      void write(const std::string& filename, const MetricCollection& collection);

      // Enhanced parsing methods
      XdpConfig parseXdpConfig(const std::string& jsonFilePath, PluginType queryPluginType);
      PluginConfig parsePluginConfig(const pt::ptree& tree, PluginType pluginType);
      
      // Plugin-specific validation
      std::vector<std::string> getSupportedModules(PluginType pluginType);
      std::vector<std::string> getSupportedSections(PluginType pluginType);
  };
} // namespace xdp

#endif