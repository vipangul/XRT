#include <unordered_map>
#include "json_parser.h"

namespace xdp {
    // JsonParser for reading and writing JSON files
    pt::ptree JsonParser::parse(const std::string& jsonFilePath) {
      // std::string jsonFilePath = "xdp.json";
      pt::ptree jsonTree;
      std::ifstream jsonFile(jsonFilePath);
      if (jsonFile.is_open()) {
        try {
          boost::property_tree::read_json(jsonFile, jsonTree);
        } catch (const pt::json_parser_error& e) {
          xrt_core::message::send(severity_level::warning, "XRT", "Failed to parse xdp.json: " + std::string(e.what()));
        }
      } else {
        xrt_core::message::send(severity_level::info, "XRT", "xdp.json not found, proceeding with default settings.");
      }
      return jsonTree;
    }

    void JsonParser::write(const std::string& filename, const MetricCollection& collection) {
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

    // Static mappings for different plugins
    const std::map<PluginType, std::vector<std::string>> JsonParser::PLUGIN_MODULES = {
        {PluginType::AIE_PROFILE, {"aie", "aie_memory", "interface_tile", "memory_tile", "microcontroller"}},
        {PluginType::AIE_TRACE,   {"aie_tile", "interface_tile", "memory_tile"}}
    };

    const std::map<PluginType, std::vector<std::string>> JsonParser::PLUGIN_SECTIONS = {
        {PluginType::AIE_PROFILE, {"tiles", "graphs"}},
        {PluginType::AIE_TRACE,   {"tiles", "graphs"}}
    };

    XdpConfig JsonParser::parseXdpConfig(const std::string& jsonFilePath,
                                         PluginType queryPluginType)
    {
        XdpConfig config;
        
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
                
                PluginConfig pluginConfig = parsePluginConfig(pluginTree, pluginType);
                if (pluginConfig.isValid) {
                    config.plugins[pluginType] = std::move(pluginConfig);
                } else {
                    xrt_core::message::send(severity_level::error, "XRT", 
                        "Failed to parse " + pluginName + ": " + pluginConfig.errorMessage);
                }
            }
            
            config.isValid = !config.plugins.empty();
            
        } catch (const std::exception& e) {
            config.errorMessage = "Parse error: " + std::string(e.what());
        }
        
        return config;
    }

    PluginConfig JsonParser::parsePluginConfig(const pt::ptree& tree, PluginType pluginType) {
        PluginConfig config;
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

    PluginType JsonParser::getPluginTypeFromString(const std::string& pluginName) {
        if (pluginName == "aie_profile") return PluginType::AIE_PROFILE;
        if (pluginName == "aie_trace") return PluginType::AIE_TRACE;
        return PluginType::UNKNOWN;
    }

    std::vector<std::string> JsonParser::getSupportedModules(PluginType pluginType) {
        auto it = PLUGIN_MODULES.find(pluginType);
        return (it != PLUGIN_MODULES.end()) ? it->second : std::vector<std::string>{};
    }

    std::vector<std::string> JsonParser::getSupportedSections(PluginType pluginType) {
        auto it = PLUGIN_SECTIONS.find(pluginType);
        return (it != PLUGIN_SECTIONS.end()) ? it->second : std::vector<std::string>{};
    }
};
