#include "json_parser.h"

namespace xdp {
    // JsonParser for reading and writing JSON files
    void JsonParser::parse(const std::string& jsonFilePath) {
      // std::string jsonFilePath = "xdp.json";
      pt::ptree jsonTree;
      std::ifstream jsonFile(jsonFilePath);
      if (jsonFile.is_open()) {
        try {
        boost::property_tree::read_json(jsonFile, jsonTree);
        // Process the JSON tree as needed
        // Example: auto value = jsonTree.get<std::string>("key");
        } catch (const pt::json_parser_error& e) {
          xrt_core::message::send(severity_level::warning, "XRT", "Failed to parse xdp.json: " + std::string(e.what()));
          // return an empty MetricCollection or handle the error as needed

        }
      } else {
        xrt_core::message::send(severity_level::info, "XRT", "xdp.json not found, proceeding with default settings.");
      }
  
      // Step 2: Generate code to get JSON object for each plugin type, read JSON object in front of AIE_profile_settings string key in JSON
      // and then parse the JSON object to get the settings for each plugin type.
      pt::ptree aieProfileSettings, aieTraceSettings;
      try {
        aieProfileSettings = jsonTree.get_child("AIE_profile_settings");
      } catch (const pt::ptree_bad_path& e) {
        xrt_core::message::send(severity_level::warning, "XRT", "AIE_profile_settings not found in JSON: " + std::string(e.what()));
      }

      // Comment out the AIE_trace_settings part for now, as it is not used in the current implementation
      // try {
      //   aieTraceSettings = jsonTree.get_child("AIE_trace_settings");
      // } catch (const pt::ptree_bad_path& e) {
      //   xrt_core::message::send(severity_level::info, "XRT", "AIE_trace_settings not found in JSON: " + std::string(e.what()));
      // }
  
      // Step 3: For each plugin type, read the settings from the JSON object and store them in a map or other data structure (To make sure json object is not empty)
      std::map<std::string, boost::property_tree::ptree> pluginSettings;
      for (const auto& setting : aieProfileSettings) {
        pluginSettings[setting.first] = setting.second;
      }

      // TODO : Also parse aieTraceSettings from aieTraceSettings ptree
  
      // Step 4: Use the stored settings to print it on console
      for (const auto& setting : pluginSettings) {
        std::ostringstream oss;
        boost::property_tree::write_json(oss, setting.second);
        std::cout << "Setting: " << setting.first << " = " << oss.str() << std::endl;
      }

      // Step 5: Create different aie profile MetricCollection objects based off the settings read from the JSON file
      // and return the MetricCollection object
      for (const auto& setting : aieProfileSettings) {
        std::cout << "\t aieProfile: Processing setting: " << setting.first << std::endl;
        if (setting.first == "tile_based_aie_tile_metrics") {
            std::cout << "\t Processing tile_based_aie_tile_metrics" << std::endl;
            auto tileBasedMetric = MetricCollection::processSettings(setting.second, metric_type::TILE_BASED_AIE_TILE);
            // check uniquePtr is valid
            
            if (!tileBasedMetric.metrics.empty()) {
              std::cout << "Adding tile_based_aie_tile_metrics to allModulesMetricCollections. Metrics size: " 
              << tileBasedMetric.metrics.size() << std::endl;
              tileBasedMetric.print();
              // pluginMetricCollections[setting.first] = std::move(tileBasedMetric);
              allModulesMetricCollections[module_type::core][setting.first] = std::move(tileBasedMetric);
            }
            else
              xrt_core::message::send(severity_level::warning, "XRT", "Failed to generate object: tile_based_aie_tile_metrics");
            std::cout << "----------------------------------------------" << std::endl;

        } else if (setting.first == "graph_based_aie_tile_metrics") {
            std::cout << "\t Processing graph_based_aie_tile_metrics" << std::endl;
            auto graphBasedMetric = MetricCollection::processSettings(setting.second, metric_type::GRAPH_BASED_AIE_TILE);
            if (!graphBasedMetric.metrics.empty()) {
              std::cout << "Adding graph_based_aie_tile_metrics to pluginMetricCollections. Metrics size: " 
              << graphBasedMetric.metrics.size() << std::endl;
              graphBasedMetric.print();
              // pluginMetricCollections[setting.first] = std::move(graphBasedMetric);
              allModulesMetricCollections[module_type::core][setting.first] = std::move(graphBasedMetric);
            }
            else
              xrt_core::message::send(severity_level::warning, "XRT", "Failed to generate object: graph_based_aie_tile_metrics");
            std::cout << "----------------------------------------------" << std::endl;
        }

        else if (setting.first == "tile_based_interface_tile_metrics") {
          std::cout << "\t Processing tile_based_interface_tile_metrics" << std::endl;
          auto tileBasedMetric = MetricCollection::processSettings(setting.second, metric_type::TILE_BASED_INTERFACE_TILE);
          // check uniquePtr is valid
          
          if (!tileBasedMetric.metrics.empty()) {
            std::cout << "Adding tile_based_interface_tile_metrics to allModulesMetricCollections. Metrics size: " 
            << tileBasedMetric.metrics.size() << std::endl;
            tileBasedMetric.print();
            // pluginMetricCollections[setting.first] = std::move(tileBasedMetric);
            allModulesMetricCollections[module_type::shim][setting.first] = std::move(tileBasedMetric);
          }
          else
            xrt_core::message::send(severity_level::warning, "XRT", "Failed to generate object: tile_based_interface_tile_metrics");
          std::cout << "----------------------------------------------" << std::endl;
        }
      }

      // write code to print allModulesMetricCollections printing everything in the map
      for (const auto& module : allModulesMetricCollections) { 
        std::cout << "----------------------------------------------" << std::endl;
        std::cout << "!!! Module: " << static_cast<int>(module.first) << std::endl;
        for (const auto& collection : module.second) {
          std::cout << "\t !!!! Plugin settings type: " << collection.first << std::endl;
          collection.second.print();
        }
        std::cout << "----------------------------------------------" << std::endl;
      }
      
      // TODO : currently only returning the first module's collection
      // return allModulesMetricCollections[module_type::core].begin()->second;
      // return allModulesMetricCollections;
      // return *this;
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

    const MetricCollection& JsonParser::getMetricCollection(module_type mod, const std::string& settingName) {
        // Check if the plugin name exists in the map
        auto it = allModulesMetricCollections.find(mod);
        if (it != allModulesMetricCollections.end()) {
            auto pluginIt = it->second.find(settingName);
            if (pluginIt != it->second.end()) {
                return pluginIt->second;
            }
        }
        throw std::runtime_error("Plugin not found: " + settingName);
    }

};
