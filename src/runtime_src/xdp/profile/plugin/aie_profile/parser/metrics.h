#ifndef METRICS_H
#define METRICS_H

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include "core/common/message.h"

namespace xdp {
namespace pt = boost::property_tree;
using severity_level = xrt_core::message::severity_level;

// Helper Functions
inline std::vector<uint8_t> parseArray(const boost::property_tree::ptree& arrayNode) {
  std::vector<uint8_t> result;
  for (const auto& item : arrayNode) {
      result.push_back(static_cast<uint8_t>(item.second.get_value<int>()));
  }
  for (const auto& item : result) {
      std::cout << "Parsed item: " << static_cast<int>(item) << std::endl;
  }
  return result;
}

// --------------------------------------------------------------------------------------------------------------------
// Base interface for all metrics
class Metric {
public:
    virtual ~Metric() = default;

    // Convert metric to ptree
    virtual boost::property_tree::ptree toPtree() const = 0;

    // Create a metric from ptree
    static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);

    virtual const std::vector<uint8_t>& getStartTile() const {
      static const std::vector<uint8_t> empty;
      return empty; // Default implementation for base class
    }
  
    virtual const std::vector<uint8_t>& getEndTile() const {
      static const std::vector<uint8_t> empty;
      return empty; // Default implementation for base class
    }
  
    virtual void print() const {
      std::cout << "Metric: " << metric;
      if (channel0) {
          std::cout << ", Channel 1: " << *channel0;
      }
      if (channel1) {
          std::cout << ", Channel 2: " << *channel1;
      }
      std::cout << std::endl;
    }
// protected:
public:
  std::string metric;
  std::optional<int> channel0;
  std::optional<int> channel1;

  bool areChannelsSet() const {
    return (channel0.has_value() && channel1.has_value());
  }

  int getChannel0() const {
    if (channel0.has_value()) {
      return *channel0;
    }
    return -1; // or throw an exception
  }

  int getChannel1() const {
    if (channel1.has_value()) {
      return *channel1;
    }
    return -1; // or throw an exception
  }
    Metric(std::string metric, std::optional<int> ch0 = std::nullopt, std::optional<int> ch1 = std::nullopt)
        : metric(std::move(metric)), channel0(ch0), channel1(ch1) {}

    // Add common fields to ptree
    void addCommonFields(boost::property_tree::ptree& obj) const {
        obj.put("metric", metric);
        if (channel0) obj.put("ch0", *channel0);
        if (channel1) obj.put("ch1", *channel1);
    }


};

// GraphBasedMetricEntry class
class GraphBasedMetricEntry : public Metric {
public:
    std::string graph;
    std::string port;

    // Constructor
    GraphBasedMetricEntry(std::string graph, std::string port, std::string metric, 
                          std::optional<int> ch0 = std::nullopt, std::optional<int> ch1 = std::nullopt)
        : Metric(std::move(metric), ch0, ch1), graph(std::move(graph)), port(std::move(port)) {}

    // Convert to ptree
    boost::property_tree::ptree toPtree() const override {
        boost::property_tree::ptree obj;
        obj.put("graph", graph);
        obj.put("port", port);
        addCommonFields(obj);
        return obj;
    }

    // Create from ptree
    static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj) {
        return std::make_unique<GraphBasedMetricEntry>(
            obj.get<std::string>("graph", "all"),
            obj.get<std::string>("port", "all"),
            obj.get<std::string>("metric", ""),
            obj.get_optional<int>("ch0") ? std::make_optional<int>(obj.get<int>("ch0")) : std::nullopt,
            obj.get_optional<int>("ch1") ? std::make_optional<int>(obj.get<int>("ch1")) : std::nullopt
        );
    }

    void print() const {
        std::cout << "^^^ print GraphBasedMetricEntry: " << graph << ", Port: " << port;
        Metric::print(); // Call the base class print method to show common fields
    }
};
// --------------------------------------------------------------------------------------------------------------------

// TileBasedMetricEntry class
class TileBasedMetricEntry : public Metric {
public:
    std::vector<uint8_t> startTile;
    std::vector<uint8_t> endTile;

    // Constructor
    TileBasedMetricEntry(std::vector<uint8_t> startTile, std::vector<uint8_t> endTile, std::string metric, 
                         std::optional<int> ch0 = std::nullopt, std::optional<int> ch1 = std::nullopt)
        : Metric(std::move(metric), ch0, ch1), startTile(std::move(startTile)), endTile(std::move(endTile)) {}

    // Convert to ptree
    boost::property_tree::ptree toPtree() const override {
        boost::property_tree::ptree obj;

        // Add startTile array
        boost::property_tree::ptree startTileNode;
        for (const auto& tile : startTile) {
            boost::property_tree::ptree tileNode;
            tileNode.put("", static_cast<int>(tile)); // Convert uint8_t to int for JSON
            startTileNode.push_back(std::make_pair("", tileNode));
        }
        obj.add_child("start", startTileNode);

        // Add endTile array
        boost::property_tree::ptree endTileNode;
        for (const auto& tile : endTile) {
            boost::property_tree::ptree tileNode;
            tileNode.put("", static_cast<int>(tile)); // Convert uint8_t to int for JSON
            endTileNode.push_back(std::make_pair("", tileNode));
        }
        obj.add_child("end", endTileNode);

        addCommonFields(obj);
        return obj;
    }



    // Create from ptree
    static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj) {
        // Helper function to parse arrays of uint8_t
        // auto parseArray = [](const boost::property_tree::ptree& arrayNode) {
        //     std::vector<uint8_t> result;
        //     for (const auto& item : arrayNode) {
        //         result.push_back(static_cast<uint8_t>(std::stoi(item.second.data())));
        //     }
        //     return result;
        // };

        return std::make_unique<TileBasedMetricEntry>(
            obj.get_child_optional("start") ? parseArray(obj.get_child("start")) : std::vector<uint8_t>{},
            obj.get_child_optional("end") ? parseArray(obj.get_child("end")) : std::vector<uint8_t>{},
            obj.get<std::string>("metric", "N/A"),
            obj.get_optional<int>("ch0") ? std::make_optional<int>(obj.get<int>("ch0")) : std::nullopt,
            obj.get_optional<int>("ch1") ? std::make_optional<int>(obj.get<int>("ch1")) : std::nullopt
        );
    }

    const std::vector<uint8_t>& getStartTile() const override {
      std::cout << "!!! TileBasedMetricEntry::getStartTile(): ";
      return startTile;
    }

    const std::vector<uint8_t>& getEndTile() const override {
      std::cout << "!!! TileBasedMetricEntry::getEndTile(): ";
      return endTile;
    }

    void print() const {
        std::cout << "^^^ print TileBasedMetricEntry: Start Tiles: ";
        for (const auto& tile : startTile) {
            std::cout << static_cast<int>(tile) << " "; // Print as int for readability
        }
        std::cout << ", End Tiles: ";
        for (const auto& tile : endTile) {
            std::cout << static_cast<int>(tile) << " "; // Print as int for readability
        }
        Metric::print(); // Call the base class print method to show common fields
    }
};

enum class metric_type {
    TILE_BASED_AIE_TILE,
    GRAPH_BASED_AIE_TILE,
    TILE_BASED_MEM_MOD,
    GRAPH_BASED_MEM_MOD,
    TILE_BASED_INTERFACE_TILE,
    GRAPH_BASED_INTERFACE_TILE,
    NUM_TYPES // Used to determine the number of metric types
};

// MetricCollection class for managing a collection of metrics
class MetricCollection {
public:
    // Currently supporting core modules, memory modules, interface tiles, 
    // memory tiles, and microcontrollers
    //static constexpr int NUM_MODULES = static_cast<int>(module_type::num_types);

    std::vector<std::unique_ptr<Metric>> metrics;

    MetricCollection() = default;

    // Delete copy constructor and copy assignment operator
    MetricCollection(const MetricCollection&) = delete;
    MetricCollection& operator=(const MetricCollection&) = delete;

    // Allow move constructor and move assignment operator
    MetricCollection(MetricCollection&&) = default;
    MetricCollection& operator=(MetricCollection&&) = default;

    // Create from ptree array
    static MetricCollection processSettings(const boost::property_tree::ptree& ptArr, 
                                            metric_type type) {
        MetricCollection collection;
        for (const auto& item : ptArr) {
            const auto& obj = item.second;

            // Directly handle metric creation based on type
            if (type == metric_type::TILE_BASED_AIE_TILE) {
                collection.metrics.push_back(TileBasedMetricEntry::processSettings(obj));
                std::cout << "!!! processed TileBasedMetricEntry from JSON : collection.metrics size: "<< collection.metrics.size() << std::endl;
            } 
            else if (type == metric_type::GRAPH_BASED_AIE_TILE) {
                collection.metrics.push_back(GraphBasedMetricEntry::processSettings(obj));
                std::cout << "!!! processed GraphBasedMetricEntry from JSON, collection.metrics size: "<< collection.metrics.size() << std::endl;
            }
            else if (type == metric_type::TILE_BASED_INTERFACE_TILE) {
              collection.metrics.push_back(TileBasedMetricEntry::processSettings(obj));
              std::cout << "!!! processed TileBasedMetricEntry from JSON : collection.metrics size: "<< collection.metrics.size() << std::endl;
            } 
            else {
                throw std::runtime_error("Unknown metric type: " + std::to_string(static_cast<int>(type)));
            }
        }
        // Print all metrics for debugging purposes
        std::cout << "## collection Added- Print and check available metrics in the collection:" << std::endl;
        for (const auto& metric : collection.metrics) {
            if (metric) {
                metric->print(); // Call the print method of each metric
            } else {
                xrt_core::message::send(severity_level::warning, "XRT", "Null metric found in collection");
            }
        }
        return collection;
    }

    // Convert to ptree array
    boost::property_tree::ptree toPtree() const {
        boost::property_tree::ptree arr;
        for (const auto& metric : metrics) {
            metric->print();
            boost::property_tree::ptree obj = metric->toPtree();
            arr.push_back(std::make_pair("", obj));
        }
        return arr;
    }

    void print() const {
        std::cout << "!!! Print MetricCollection:" << std::endl;
        for (const auto& metric : metrics) {
            if (metric) {
                metric->print();
            } else {
                xrt_core::message::send(severity_level::warning, "XRT", "Null metric found in collection");
            }
        }
    }
};

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
    void parse(const std::string& jsonFilePath) {
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

    static void write(const std::string& filename, const MetricCollection& collection) {
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

    const MetricCollection& getMetricCollection(module_type mod, const std::string& settingName) {
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

// Define the static member
// std::map<std::string, MetricCollection> JsonParser::pluginMetricCollections;
// std::map<module_type, std::map<std::string, MetricCollection>> JsonParser::allModulesMetricCollections;
}

#endif