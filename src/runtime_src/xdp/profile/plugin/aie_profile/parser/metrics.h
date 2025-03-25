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

namespace pt = boost::property_tree;
using severity_level = xrt_core::message::severity_level;
// Base interface for all metrics
class Metric {
public:
    virtual ~Metric() = default;

    // Convert metric to ptree
    virtual boost::property_tree::ptree toPtree() const = 0;

    // Create a metric from ptree
    static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);

protected:
    std::string metric;
    std::optional<int> channel1;
    std::optional<int> channel2;

    Metric(std::string metric, std::optional<int> ch1 = std::nullopt, std::optional<int> ch2 = std::nullopt)
        : metric(std::move(metric)), channel1(ch1), channel2(ch2) {}

    // Add common fields to ptree
    void addCommonFields(boost::property_tree::ptree& obj) const {
        obj.put("metric", metric);
        if (channel1) obj.put("ch1", *channel1);
        if (channel2) obj.put("ch2", *channel2);
    }
};

// GraphBasedMetricEntry class
class GraphBasedMetricEntry : public Metric {
public:
    std::string graph;
    std::string port;

    // Constructor
    GraphBasedMetricEntry(std::string graph, std::string port, std::string metric, 
                          std::optional<int> ch1 = std::nullopt, std::optional<int> ch2 = std::nullopt)
        : Metric(std::move(metric), ch1, ch2), graph(std::move(graph)), port(std::move(port)) {}

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
            obj.get_optional<int>("ch1") ? std::make_optional<int>(obj.get<int>("ch1")) : std::nullopt,
            obj.get_optional<int>("ch2") ? std::make_optional<int>(obj.get<int>("ch2")) : std::nullopt
        );
    }
};

// TileBasedMetricEntry class
class TileBasedMetricEntry : public Metric {
public:
    std::vector<uint8_t> startTile;
    std::vector<uint8_t> endTile;

    // Constructor
    TileBasedMetricEntry(std::vector<uint8_t> startTile, std::vector<uint8_t> endTile, std::string metric, 
                         std::optional<int> ch1 = std::nullopt, std::optional<int> ch2 = std::nullopt)
        : Metric(std::move(metric), ch1, ch2), startTile(std::move(startTile)), endTile(std::move(endTile)) {}

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
        obj.add_child("startTile", startTileNode);

        // Add endTile array
        boost::property_tree::ptree endTileNode;
        for (const auto& tile : endTile) {
            boost::property_tree::ptree tileNode;
            tileNode.put("", static_cast<int>(tile)); // Convert uint8_t to int for JSON
            endTileNode.push_back(std::make_pair("", tileNode));
        }
        obj.add_child("endTile", endTileNode);

        addCommonFields(obj);
        return obj;
    }

    // Create from ptree
    static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj) {
        // Helper function to parse arrays of uint8_t
        auto parseArray = [](const boost::property_tree::ptree& arrayNode) {
            std::vector<uint8_t> result;
            for (const auto& item : arrayNode) {
                result.push_back(static_cast<uint8_t>(std::stoi(item.second.data())));
            }
            return result;
        };

        return std::make_unique<TileBasedMetricEntry>(
            obj.get_child_optional("startTile") ? parseArray(obj.get_child("startTile")) : std::vector<uint8_t>{},
            obj.get_child_optional("endTile") ? parseArray(obj.get_child("endTile")) : std::vector<uint8_t>{},
            obj.get<std::string>("metric", ""),
            obj.get_optional<int>("ch1") ? std::make_optional<int>(obj.get<int>("ch1")) : std::nullopt,
            obj.get_optional<int>("ch2") ? std::make_optional<int>(obj.get<int>("ch2")) : std::nullopt
        );
    }
};

// MetricCollection class for managing a collection of metrics
class MetricCollection {
public:
    std::vector<std::unique_ptr<Metric>> metrics;

    MetricCollection() = default;

    // Delete copy constructor and copy assignment operator
    MetricCollection(const MetricCollection&) = delete;
    MetricCollection& operator=(const MetricCollection&) = delete;

    // Allow move constructor and move assignment operator
    MetricCollection(MetricCollection&&) = default;
    MetricCollection& operator=(MetricCollection&&) = default;

    // Create from ptree array
    static MetricCollection processSettings(const boost::property_tree::ptree& ptArr) {
        MetricCollection collection;
        for (const auto& item : ptArr) {
            const auto& obj = item.second;
            std::string type = obj.get<std::string>("type");

            // Directly handle metric creation based on type
            if (type == "tile_based_aie_tile_metrics") {
                collection.metrics.push_back(TileBasedMetricEntry::processSettings(obj));
            } 
            else if (type == "graph_based_aie_tile_metrics") {
                collection.metrics.push_back(GraphBasedMetricEntry::processSettings(obj));
            } 
            else {
                throw std::runtime_error("Unknown metric type: " + type);
            }
        }
        return collection;
    }

    // Convert to ptree array
    boost::property_tree::ptree toPtree() const {
        boost::property_tree::ptree arr;
        for (const auto& metric : metrics) {
            boost::property_tree::ptree obj = metric->toPtree();
            arr.push_back(std::make_pair("", obj));
        }
        return arr;
    }
};

// JsonParser for reading and writing JSON files
class JsonParser {
  private:
    // Data structure to store MetricCollection objects for different plugin types
    static std::map<std::string, MetricCollection> pluginMetricCollections;
public:
    static MetricCollection& parse(const std::string& jsonFilePath) {
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

      try {
        aieTraceSettings = jsonTree.get_child("AIE_trace_settings");
      } catch (const pt::ptree_bad_path& e) {
        xrt_core::message::send(severity_level::info, "XRT", "AIE_trace_settings not found in JSON: " + std::string(e.what()));
      }
  
      // Step 3: For each plugin type, read the settings from the JSON object and store them in a map or other data structure
      std::map<std::string, std::string> pluginSettings;
      for (const auto& setting : aieProfileSettings) {
        pluginSettings[setting.first] = setting.second.data();
      }
  
      // Step 4: Use the stored settings to print it on console
      for (const auto& setting : pluginSettings) {
        std::cout << "Setting: " << setting.first << " = " << setting.second << std::endl;
      }

      // Step 5: Create different aie profile MetricCollection objects based off the settings read from the JSON file
      // and return the MetricCollection object
      for (const auto& setting : aieProfileSettings) {
        if (setting.first == "tile_based_aie_tile_metrics") {
            // aieProfileSettings = aieTraceSettings;
            // store in pluginMetricCollections 
            pluginMetricCollections[setting.first] = MetricCollection::processSettings(setting.second);

        } else if (setting.first == "graph_based_aie_tile_metrics") {
            // aieProfileSettings = aieTraceSettings;
            pluginMetricCollections[setting.first] = MetricCollection::processSettings(setting.second);
        }
      }

      
      // return MetricCollection::processSettings(pt);
      return pluginMetricCollections["tile_based_aie_tile_metrics"];
    }

    static void write(const std::string& filename, const MetricCollection& collection) {
        std::ofstream file(filename);
        if (!file) {
            throw std::runtime_error("Error writing to file: " + filename);
        }

        boost::property_tree::ptree pt = collection.toPtree();
        boost::property_tree::write_json(file, pt);
    }
};

#endif