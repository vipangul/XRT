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
        std::cout << "!!! After: Writing MetricCollection to JSON file: " << filename << std::endl;
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
            xrt_core::message::send(severity_level::info, "XRT", 
                "Successfully parsed JSON file: " + jsonFilePath);
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
    const std::map<PluginType, std::vector<std::string>> SettingsJsonParser::PLUGIN_MODULES = {
        {PluginType::AIE_PROFILE, {"aie", "aie_memory", "interface_tile", "memory_tile", "microcontroller"}},
        {PluginType::AIE_TRACE,   {"aie_tile", "interface_tile", "memory_tile"}}
    };

    const std::map<PluginType, std::vector<std::string>> SettingsJsonParser::PLUGIN_SECTIONS = {
        {PluginType::AIE_PROFILE, {"tiles", "graphs"}},
        {PluginType::AIE_TRACE,   {"tiles", "graphs"}}
    };

    XdpJsonSetting SettingsJsonParser::parseXdpJsonSetting(const std::string& jsonFilePath,
                                         PluginType queryPluginType)
    {
        XdpJsonSetting config;
        
        try {
            pt::ptree jsonTree = parse(jsonFilePath);
            
            // Parse each plugin section
            for (const auto& [pluginName, pluginTree] : jsonTree) {
                PluginType pluginType = getPluginTypeFromString(pluginName);
                
                if (pluginType == PluginType::UNKNOWN) {
                    xrt_core::message::send(severity_level::warning, "XRT", 
                        "Unknown plugin: " + pluginName);
                    continue;
                }
                // Skip if plugin type does not match
                if (pluginType != queryPluginType) {
                  // TODO: Delete after testing
                  xrt_core::message::send(severity_level::warning, "XRT", 
                                      "Skip this plugin, Mismatched plugin type: " + pluginName);
                  continue;
                }
                
                PluginJsonSetting PluginJsonSetting = parsePluginJsonSetting(pluginTree, pluginType);
                if (PluginJsonSetting.isValid) {
                    config.plugins[pluginType] = std::move(PluginJsonSetting);
                } else {
                    xrt_core::message::send(severity_level::error, "XRT", 
                        "Failed to parse " + pluginName + ": " + PluginJsonSetting.errorMessage);
                }
            }
            
            config.isValid = !config.plugins.empty();
            
        } catch (const std::exception& e) {
            config.errorMessage = "Parse error: " + std::string(e.what());
        }
        
        return config;
    }

    PluginJsonSetting SettingsJsonParser::parsePluginJsonSetting(const pt::ptree& tree, PluginType pluginType) {
        PluginJsonSetting config;
        config.type = pluginType;
        
        try {
            // if (!validatePluginSchema(tree, pluginType)) {
            //     config.errorMessage = "Invalid schema for plugin";
            //     return config;
            // }
            
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
                    for (const auto& item : moduleArray) {
                        ValidationResult result = validateMetricEntry(item.second, moduleKey);
                        for (const auto& error : result.errors) {
                            xrt_core::message::send(severity_level::error, "XRT", 
                                "Schema error in " + moduleKey + ": " + error);
                        }
                        for (const auto& warning : result.warnings) {
                            xrt_core::message::send(severity_level::warning, "XRT", 
                                "Schema warning in " + moduleKey + ": " + warning);
                        }
                        if (!result.isValid) {
                          continue;
                        }
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

                    std::vector<pt::ptree> metrics;
                    for (const auto& item : moduleArray) {
                        metrics.push_back(item.second);
                    }
                    config.sections[sectionKey][moduleKey] = metrics;
                }
            }
            
            config.isValid = true;
            
        } catch (const std::exception& e) {
            config.errorMessage = "Parse error: " + std::string(e.what());
        }
        
        return config;
    }

    PluginType SettingsJsonParser::getPluginTypeFromString(const std::string& pluginName) {
        if (pluginName == "aie_profile") return PluginType::AIE_PROFILE;
        if (pluginName == "aie_trace") return PluginType::AIE_TRACE;
        return PluginType::UNKNOWN;
    }

    std::vector<std::string> SettingsJsonParser::getSupportedModules(PluginType pluginType) {
        auto it = PLUGIN_MODULES.find(pluginType);
        return (it != PLUGIN_MODULES.end()) ? it->second : std::vector<std::string>{};
    }

    std::vector<std::string> SettingsJsonParser::getSupportedSections(PluginType pluginType) {
        auto it = PLUGIN_SECTIONS.find(pluginType);
        return (it != PLUGIN_SECTIONS.end()) ? it->second : std::vector<std::string>{};
    }

    const std::map<std::string, std::vector<SchemaField>> SettingsJsonParser::MODULE_SCHEMAS = {
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
        SchemaField("channels", false, "array"),
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
    }}
  };

  ValidationResult SettingsJsonParser::validateMetricEntry(const pt::ptree& entry, const std::string& moduleName) {
    ValidationResult result;
    
    auto schema = getSchemaForModule(moduleName);
    
    for (const auto& field : schema) {
        ValidationResult fieldResult = validateField(entry, field);
        result.errors.insert(result.errors.end(), fieldResult.errors.begin(), fieldResult.errors.end());
        result.warnings.insert(result.warnings.end(), fieldResult.warnings.begin(), fieldResult.warnings.end());
        if (!fieldResult.isValid) {
            result.isValid = false;
        }
    }
    
    return result;
}

ValidationResult SettingsJsonParser::validateField(const pt::ptree& entry, const SchemaField& field) {
    ValidationResult result;
    
    auto fieldOpt = entry.get_child_optional(field.name);
    
    if (field.required && fieldOpt == boost::none) {
        result.addError("Required field '" + field.name + "' is missing");
        return result;
    }
    
    // if (fieldOpt == boost::none) {
    //     return result; // Optional field not present
    // }
    
    try {
        if (field.type == "string") {
            fieldOpt->get_value<std::string>();
        } else if (field.type == "array" && field.name == "channels") {
            if (!isValidChannelArray(*fieldOpt)) {
                result.addError("Invalid channels array format");
            }
        }
    } catch (const std::exception& e) {
        result.addError("Invalid value for field '" + field.name + "': " + e.what());
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

std::vector<SchemaField> SettingsJsonParser::getSchemaForModule(const std::string& moduleName) const {
    auto it = MODULE_SCHEMAS.find(moduleName);
    return (it != MODULE_SCHEMAS.end()) ? it->second : std::vector<SchemaField>{};
}

};
