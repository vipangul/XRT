#include <unordered_map>
#include <filesystem>
#include "json_parser.h"

namespace xdp {

    void SettingsJsonParser::write(const std::string& filename, const MetricCollection& collection) {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Error writing to file: " + filename);
        }

        // Print collection 
        // std::cout << "!!! After: Writing MetricCollection to JSON file: " << filename << std::endl;
        // Print all metrics for debugging purposes
        for (const auto& metric : collection.metrics) {
            if (metric) {
                metric->print(); // Call the print method of each metric
            } else {
                xrt_core::message::send(severity_level::warning, "XRT", "Null metric found in collection");
            }
        }
      
        boost::property_tree::ptree pt = collection.toPtree();
        boost::property_tree::write_json(file, pt);
    }

    // Implementation
    JsonParseResult SettingsJsonParser::parseWithStatus(const std::string& jsonFilePath) {
        JsonParseResult result;
        
        // Check if file exists
        if (!std::filesystem::exists(jsonFilePath)) {
            result.errorMessage = "File not found: " + jsonFilePath;
            xrt_core::message::send(severity_level::info, "XRT", 
                result.errorMessage + ", proceeding with default settings.");
            return result;
        }
        
        // Check if it's a regular file
        if (!std::filesystem::is_regular_file(jsonFilePath)) {
            result.errorMessage = "Path exists but is not a regular file: " + jsonFilePath;
            xrt_core::message::send(severity_level::warning, "XRT", result.errorMessage);
            return result;
        }
        
        std::ifstream jsonFile(jsonFilePath);
        if (!jsonFile.is_open()) {
            result.errorMessage = "Failed to open file: " + jsonFilePath;
            xrt_core::message::send(severity_level::warning, "XRT", result.errorMessage);
            return result;
        }
        
        try {
            boost::property_tree::read_json(jsonFile, result.tree);
            result.success = true;
            // xrt_core::message::send(severity_level::info, "XRT", 
            //     "Successfully parsed JSON file: " + jsonFilePath);
        } catch (const pt::json_parser_error& e) {
            result.errorMessage = "JSON parse error: " + std::string(e.what());
            xrt_core::message::send(severity_level::error, "XRT", 
                "Failed to parse JSON file '" + jsonFilePath + "': " + result.errorMessage);
            result.tree.clear();
        }
        return result;
    }
    
    // Backward compatible version
    pt::ptree SettingsJsonParser::parse(const std::string& jsonFilePath) {
        return parseWithStatus(jsonFilePath).tree;
    }

    bool SettingsJsonParser::isValidJson(const std::string& jsonFilePath) {
        try {
            return parseWithStatus(jsonFilePath).isValid();
        } catch (const std::exception& e) {
            xrt_core::message::send(severity_level::warning, "XRT", 
                "Invalid JSON file: " + std::string(e.what()));
            return false;
        }
    }

    // Static mappings for different plugins
    const std::map<uint64_t, std::vector<std::string>> SettingsJsonParser::PLUGIN_MODULES = {
        {info::aie_profile, {"aie", "aie_memory", "interface_tile", "memory_tile", "microcontroller"}},
        {info::aie_trace,   {"aie_tile", "interface_tile", "memory_tile"}}
    };

    const std::map<uint64_t, std::vector<std::string>> SettingsJsonParser::PLUGIN_SECTIONS = {
        {info::aie_profile, {"tiles", "graphs"}},
        {info::aie_trace,   {"tiles", "graphs"}}
    };

    const std::map<std::string, std::vector<SchemaField>> SettingsJsonParser::MODULE_SCHEMAS_GRAPH_BASED = {
    {"aie", {
        SchemaField("graph", true, "string"),
        SchemaField("kernel", true, "string"),
        SchemaField("metric", true, "string"),
        SchemaField("channels", false, "array")
    }},
    {"aie_memory", {
        SchemaField("graph", true, "string"),
        SchemaField("kernel", true, "string"),
        SchemaField("metric", true, "string"),
        SchemaField("channels", false, "array")
    }},
    {"memory_tile", {
        SchemaField("graph", true, "string"),
        SchemaField("buffer", true, "string"),
        SchemaField("metric", true, "string"),
        SchemaField("channels", false, "array")
    }},
    {"interface_tile", {
        SchemaField("graph", true, "string"),
        SchemaField("port", true, "string"),
        SchemaField("metric", true, "string"),
        SchemaField("channels", false, "array"),
        SchemaField("bytes", false, "string")
    }},
    {"microcontroller", {
        SchemaField("graph", true, "string"),
        SchemaField("metric", true, "string")
    }}
  };

  const std::map<std::string, std::vector<SchemaField>> SettingsJsonParser::MODULE_SCHEMAS_TILE_BASED = {
    {"aie", {
        SchemaField("metric", true, "string"),
        SchemaField("col", false, "int"),
        SchemaField("row", false, "int"),
        SchemaField("all_tiles", false, "bool"),
        SchemaField("start", false, "array"),
        SchemaField("end", false, "array"),
        SchemaField("channels", false, "array")
    }},
    {"aie_memory", {
        SchemaField("metric", true, "string"),
        SchemaField("col", false, "int"),
        SchemaField("row", false, "int"),
        SchemaField("all_tiles", false, "bool"),
        SchemaField("start", false, "array"),
        SchemaField("end", false, "array"),
        SchemaField("channels", false, "array")
    }},
    {"memory_tile", {
        SchemaField("metric", true, "string"),
        SchemaField("col", false, "int"),
        SchemaField("row", false, "int"),
        SchemaField("all_tiles", false, "bool"),
        SchemaField("start", false, "array"),
        SchemaField("end", false, "array"),
        SchemaField("channels", false, "array")
    }},
    {"interface_tile", {
        SchemaField("metric", true, "string"),
        SchemaField("col", false, "int"),
        SchemaField("row", false, "int"),
        SchemaField("all_tiles", false, "bool"),
        SchemaField("start", false, "array"),
        SchemaField("end", false, "array"),
        SchemaField("channels", false, "array"),
        SchemaField("bytes", false, "string")
    }},
    {"microcontroller", {
        SchemaField("metric", true, "string"),
        SchemaField("col", false, "int"),
        SchemaField("row", false, "int"),
        SchemaField("all_tiles", false, "bool"),
        SchemaField("start", false, "array"),
        SchemaField("end", false, "array")
    }}
  };

    XdpJsonSetting SettingsJsonParser::parseXdpJsonSetting(const std::string& jsonFilePath,
                                         uint64_t queryPluginType)
    {
        XdpJsonSetting config;
        
        try {
            pt::ptree jsonTree = parse(jsonFilePath);
            
            // Check for version field (optional)
            auto versionOpt = jsonTree.get_optional<std::string>("version");
            if (versionOpt) {
                config.version = *versionOpt;
                xrt_core::message::send(severity_level::info, "XRT", 
                    "JSON configuration version: " + config.version);
            }
            
            // Parse each plugin section
            for (const auto& [pluginName, pluginTree] : jsonTree) {
                // Skip version field - it's handled separately
                if (pluginName == "version") {
                    continue;
                }
                
                uint64_t pluginType = getPluginTypeFromString(pluginName);
                
                if (pluginType == 0) {
                  std::stringstream msg;
                  msg << "Unknown plugin name specified: " << pluginName;
                  config.errorMessage = msg.str();
                  xrt_core::message::send(severity_level::warning, "XRT", msg.str());
                    continue;
                }
                // Skip if plugin type does not match
                if (pluginType != queryPluginType) {
                  xrt_core::message::send(severity_level::debug, "XRT", 
                                      "Skip this plugin, Mismatched plugin type: " + pluginName);
                  continue;
                }
                
                PluginJsonSetting pluginSettings = parsePluginJsonSetting(pluginTree, pluginType);
                if (pluginSettings.isValid) {
                    config.plugins[pluginType] = std::move(pluginSettings);
                } else {
                    xrt_core::message::send(severity_level::error, "XRT", 
                        "Failed to parse " + pluginName + ": " + pluginSettings.errorMessage);
                }
            }
            
            config.isValid = !config.plugins.empty();
            
        } catch (const std::exception& e) {
            config.errorMessage = "Parse error: " + std::string(e.what());
        }
        
        return config;
    }

    PluginJsonSetting SettingsJsonParser::parsePluginJsonSetting(const pt::ptree& tree, uint64_t pluginType) {
        PluginJsonSetting config;
        config.pluginType = pluginType;
        
        try {
            auto supportedSections = getSupportedSections(pluginType);
            auto supportedModules  = getSupportedModules(pluginType);

            // Track modules to detect conflicts - map module to first section it appears in
            // PLUGIN_MODULES -> sectionName ("tiles", "graphs")
            std::unordered_map<std::string, std::string> moduleToFirstSection;
            
            // Parse sections (tiles, graphs)
            for (const auto& [sectionKey, section] : tree) {
                
                if (std::find(supportedSections.begin(), supportedSections.end(), sectionKey) == supportedSections.end()) {
                    xrt_core::message::send(severity_level::warning, "XRT", 
                        "Unsupported section for this plugin: " + sectionKey);
                    continue;
                }
                
                // Parse modules within section
                for (const auto& [moduleKey, moduleArray] : section) {
                    if (std::find(supportedModules.begin(), supportedModules.end(), moduleKey) == supportedModules.end()) {
                        xrt_core::message::send(severity_level::warning, "XRT", 
                            "Unsupported module for this plugin: " + moduleKey);
                        continue;
                    }

                    // Validate each metric entry
                    std::vector<pt::ptree> metrics;
                    for (const auto& item : moduleArray) {
                        ValidationResult result = validateMetricEntry(item.second, moduleKey, sectionKey);
                        for (const auto& error : result.errors) {
                            xrt_core::message::send(severity_level::error, "XRT", 
                                "JSON schema error in module " + moduleKey + ": " + error);
                        }
                        for (const auto& warning : result.warnings) {
                            xrt_core::message::send(severity_level::warning, "XRT", 
                                "JSON schema warning in module " + moduleKey + ": " + warning);
                        }
                        if (!result.isValid) {
                          continue;
                        }

                        // Check for conflict - same module in different section
                        if (moduleToFirstSection.find(moduleKey) != moduleToFirstSection.end()) {
                          std::string firstSection = moduleToFirstSection[moduleKey];
                          if (firstSection != sectionKey) {
                              std::stringstream warningMsg;
                              warningMsg << "Warning: Module '" << moduleKey  << "' appears in both '"
                                        << firstSection + "' and '" + sectionKey
                                        << "' sections. Using configuration from '" << firstSection
                                        << "' section and ignoring '" + sectionKey + "' configuration.";
                              xrt_core::message::send(severity_level::warning, "XRT", warningMsg.str());
                              continue; // Skip this duplicate module in later section
                          }
                        } else {
                          // First time seeing this module - record which section it's in
                          moduleToFirstSection[moduleKey] = sectionKey;
                        }
                        
                        metrics.push_back(item.second);
                     } // end for each metric in moduleArray
                     
                     if (metrics.empty()) {
                        xrt_core::message::send(severity_level::warning, "XRT", 
                            "No valid metrics found for module: " + moduleKey);
                        continue;
                     }
                     config.sections[sectionKey][moduleKey] = metrics;
                     config.isValid = true;
                } // end for each module in section
            } // end for each section (tiles, graphs)
        } catch (const std::exception& e) {
            config.errorMessage = "Parse error: " + std::string(e.what());
        }
        
        return config;
    }

    uint64_t SettingsJsonParser::getPluginTypeFromString(const std::string& pluginName) {
        if (pluginName == "aie_profile") return info::aie_profile;
        if (pluginName == "aie_trace") return info::aie_trace;
        return 0; // Unknown plugin type
    }

    std::vector<std::string> SettingsJsonParser::getSupportedModules(uint64_t pluginType) {
        auto it = PLUGIN_MODULES.find(pluginType);
        return (it != PLUGIN_MODULES.end()) ? it->second : std::vector<std::string>{};
    }

    std::vector<std::string> SettingsJsonParser::getSupportedSections(uint64_t pluginType) {
        auto it = PLUGIN_SECTIONS.find(pluginType);
        return (it != PLUGIN_SECTIONS.end()) ? it->second : std::vector<std::string>{};
    }

  ValidationResult SettingsJsonParser::validateMetricEntry(const pt::ptree& entry, const std::string& moduleName, 
                                                           const std::string& sectionType) {
    ValidationResult result;
    
    auto schema = getSchemaForModule(moduleName, sectionType);
    
    for (const auto& field : schema) {
        ValidationResult fieldResult = validateField(entry, field);
        result.errors.insert(result.errors.end(), fieldResult.errors.begin(), fieldResult.errors.end());
        result.warnings.insert(result.warnings.end(), fieldResult.warnings.begin(), fieldResult.warnings.end());
        if (!fieldResult.isValid) {
            result.isValid = false;
        }
    }
    
    // Additional validation for tile-based entries
    if (sectionType == "tiles") {
        // Check that we have valid tile specification
        bool hasAllTiles = entry.get_optional<bool>("all_tiles").value_or(false);
        bool hasColRow = entry.get_optional<int>("col") && entry.get_optional<int>("row");
        bool hasRange = entry.get_child_optional("start") && entry.get_child_optional("end");
        bool hasSingleCol = entry.get_optional<int>("col") && !entry.get_optional<int>("row");
        
        // For microcontroller, only col or all_tiles is needed
        if (moduleName == "microcontroller") {
            if (!hasAllTiles && !hasSingleCol && !hasRange) {
                result.addError("Tile specification required: either 'all_tiles': true, 'col': <num>, or 'start'/'end' range");
            }
        } else {
            // For other modules, need proper tile specification
            if (!hasAllTiles && !hasColRow && !hasRange) {
                result.addError("Tile specification required: either 'all_tiles': true, 'col'/'row' pair, or 'start'/'end' range");
            }
        }
        
        // Validate tile range if present
        if (hasRange && !isValidTileRange(entry)) {
            result.addError("Invalid tile range specification");
        }
    }
    
    return result;
}

ValidationResult SettingsJsonParser::validateField(const pt::ptree& entry, const SchemaField& field) {
    ValidationResult result;
    
    auto fieldOpt = entry.get_child_optional(field.name);
    
    if (field.required && fieldOpt == boost::none) {
        result.addError("Required field '" + field.name + "' is missing");
        result.isValid = false;
        return result;
    }
    
    if (fieldOpt == boost::none) {
        return result; // Optional field not present
    }
    
    try {
        if (field.type == "string") {
            std::string value = fieldOpt->get_value<std::string>();
            // Check allowed values if specified
            if (!field.allowedValues.empty()) {
                if (std::find(field.allowedValues.begin(), field.allowedValues.end(), value) == field.allowedValues.end()) {
                    result.addError("Invalid value '" + value + "' for field '" + field.name + "'");
                }
            }
        } else if (field.type == "int") {
            fieldOpt->get_value<int>();
        } else if (field.type == "bool") {
            fieldOpt->get_value<bool>();
        } else if (field.type == "array") {
            if (field.name == "channels") {
                if (!isValidChannelArray(*fieldOpt)) {
                    result.addError("Invalid channels array format");
                }
            } else if (field.name == "start" || field.name == "end") {
                // Validate tile coordinate array
                if (fieldOpt->size() != 2) {
                    result.addError("Tile coordinate array must have exactly 2 elements [col, row]");
                } else {
                    for (const auto& coord : *fieldOpt) {
                        coord.second.get_value<int>(); // Will throw if not int
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        result.addError("Invalid value for field '" + field.name + "': " + e.what());
        result.isValid = false;
    }
    
    return result;
}

bool SettingsJsonParser::isValidChannelArray(const pt::ptree& channelsArray) const {
    try {
        for (const auto& channelNode : channelsArray) {
            int channel = channelNode.second.get_value<int>();
            if (channel < 0 || channel > 255) {
                return false;
            }
        }
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

std::vector<SchemaField> SettingsJsonParser::getSchemaForModule(const std::string& moduleName, 
                                                               const std::string& sectionType) const {
    const auto& schemas = (sectionType == "graphs") ? MODULE_SCHEMAS_GRAPH_BASED : MODULE_SCHEMAS_TILE_BASED;
    auto it = schemas.find(moduleName);
    return (it != schemas.end()) ? it->second : std::vector<SchemaField>{};
}

bool SettingsJsonParser::isValidTileRange(const pt::ptree& entry) const {
    try {
        auto startOpt = entry.get_child_optional("start");
        auto endOpt = entry.get_child_optional("end");
        
        if (!startOpt || !endOpt) {
            return false;
        }
        
        if (startOpt->size() != 2 || endOpt->size() != 2) {
            return false;
        }
        
        // Get coordinates
        std::vector<int> startCoords, endCoords;
        for (const auto& coord : *startOpt) {
            startCoords.push_back(coord.second.get_value<int>());
        }
        for (const auto& coord : *endOpt) {
            endCoords.push_back(coord.second.get_value<int>());
        }
        
        // Validate range (start <= end)
        return (startCoords[0] <= endCoords[0] && startCoords[1] <= endCoords[1]);
        
    } catch (const std::exception&) {
        return false;
    }
}

};
