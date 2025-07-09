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
#include "xdp/profile/plugin/vp_base/info.h"
#include "xdp/profile/database/static_info/aie_constructs.h"

namespace xdp {
  namespace pt = boost::property_tree;
  using severity_level = xrt_core::message::severity_level;

  // Datastructures for JSON validation
    struct ValidationResult {
      bool isValid = true;
      std::vector<std::string> errors;
      std::vector<std::string> warnings;
      
      void addError(const std::string& message) {
          errors.push_back(message);
          isValid = false;
      }
      
      void addWarning(const std::string& message) {
          warnings.push_back(message);
      }
  };

  struct SchemaField {
      std::string name; // Json field name
      bool required;    // Is this field required?
      std::string type; // Expected type of the field (e.g. "string", "int", "array", etc.)
      
      SchemaField(const std::string& n, bool req, const std::string& t)
          : name(n), required(req), type(t) {}
  };

  struct PluginJsonSetting {
      uint64_t pluginType;
      // "tiles"/"graphs" , <aie/aie_memory/memory_tile/interface_tile>, <JSON objects>>
      std::map<std::string, std::map<std::string, std::vector<pt::ptree>>> sections;
      bool isValid = false;
      std::string errorMessage;
  };

  struct XdpJsonSetting {
      std::map<uint64_t, PluginJsonSetting> plugins;
      bool isValid = false;
      std::string errorMessage;
  };

  struct JsonParseResult {
    pt::ptree tree;
    bool success = false;
    std::string errorMessage;

    bool isValid() const { return success; }
    bool isEmpty() const { return tree.empty(); }
};

  // SettingsJsonParser for reading and writing JSON files
  class SettingsJsonParser {
    private:
      SettingsJsonParser() = default;
      SettingsJsonParser(const SettingsJsonParser&) = delete;
      SettingsJsonParser& operator=(const SettingsJsonParser&) = delete;

      // Plugin-specific module mappings
      static const std::map<uint64_t, std::vector<std::string>> PLUGIN_MODULES;
      static const std::map<uint64_t, std::vector<std::string>> PLUGIN_SECTIONS;
      
      JsonParseResult parseWithStatus(const std::string& jsonFilePath);

      // Plugin-specific validation
      uint64_t getPluginTypeFromString(const std::string& pluginName);
      std::vector<std::string> getSupportedModules(uint64_t pluginType);
      std::vector<std::string> getSupportedSections(uint64_t pluginType);

      // JSON validation methods
      static const std::map<std::string, std::vector<SchemaField>> MODULE_SCHEMAS;
      
      bool validatePluginSchema(const pt::ptree& tree, uint64_t pluginType);
      ValidationResult validateMetricEntry(const pt::ptree& entry, const std::string& moduleName);
      ValidationResult validateField(const pt::ptree& entry, const SchemaField& field);
      bool isValidChannelArray(const pt::ptree& channelsArray) const;
      std::vector<SchemaField> getSchemaForModule(const std::string& moduleName) const;
      
    public:
      static SettingsJsonParser& getInstance() {
        static SettingsJsonParser instance;
        return instance;
      }

      pt::ptree parse(const std::string& jsonFilePath);
      bool isValidJson(const std::string& jsonFilePath);
      void write(const std::string& filename, const MetricCollection& collection);

      // Enhanced parsing methods
      XdpJsonSetting parseXdpJsonSetting(const std::string& jsonFilePath, uint64_t queryPluginType);
      PluginJsonSetting parsePluginJsonSetting(const pt::ptree& tree, uint64_t pluginType);
      
  };
} // namespace xdp

#endif