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

// Base interface for all metrics
class Metric {
public:
    virtual ~Metric() = default;

    // Convert metric to ptree
    virtual boost::property_tree::ptree toPtree() const = 0;

    // Create a metric from ptree
    static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
};

// MetricEntry class implementing Metric
class MetricEntry : public Metric {
public:
  std::string graph;
  std::string port;
  std::string metric;
  std::string startTile;
  std::string endTile;
  std::optional<int> channel1;
  std::optional<int> channel2;

  // Constructor
  MetricEntry(std::string graph, std::string port, std::string metric, 
        std::string startTile = "", std::string endTile = "",
        std::optional<int> ch1 = std::nullopt, std::optional<int> ch2 = std::nullopt)
    : graph(std::move(graph)), port(std::move(port)), metric(std::move(metric)), 
      startTile(std::move(startTile)), endTile(std::move(endTile)), 
      channel1(ch1), channel2(ch2) {}

  // Convert to ptree
  boost::property_tree::ptree toPtree() const override {
    boost::property_tree::ptree obj;
    obj.put("gr", graph);
    obj.put("pt", port);
    obj.put("metric", metric);
    if (!startTile.empty()) obj.put("startTile", startTile);
    if (!endTile.empty()) obj.put("endTile", endTile);
    if (channel1) obj.put("ch1", *channel1);
    if (channel2) obj.put("ch2", *channel2);
    return obj;
  }

  // // Create from ptree
  // static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj) {
  //   return std::make_unique<MetricEntry>(
  //     obj.get<std::string>("graph", "all"),
  //     obj.get<std::string>("port", "all"),
  //     obj.get<std::string>("metric", ""),
  //     obj.get<std::string>("startTile", "all"),
  //     obj.get<std::string>("endTile", "all"),
  //     obj.get_optional<int>("channel1"),
  //     obj.get_optional<int>("channel2")
  //   );
  // }

    // Create from ptree
  static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj) {
      return std::make_unique<MetricEntry>(
          obj.get<std::string>("graph", "all"),
          obj.get<std::string>("port", "all"),
          obj.get<std::string>("metric", ""),
          obj.get<std::string>("startTile", "all"),
          obj.get<std::string>("endTile", "all"),
          obj.get_optional<int>("channel1") ? std::make_optional<int>(obj.get<int>("channel1")) : std::nullopt,
          obj.get_optional<int>("channel2") ? std::make_optional<int>(obj.get<int>("channel2")) : std::nullopt
      );
  }
};;

// MetricCollection class for managing a collection of metrics
class MetricCollection {
public:
    std::vector<std::unique_ptr<Metric>> metrics;

    // Create from ptree array
    static MetricCollection processSettings(const boost::property_tree::ptree& ptArr) {
        MetricCollection collection;
        for (const auto& item : ptArr) {
            const auto& obj = item.second;
            std::string type = obj.get<std::string>("type");

            // Directly handle metric creation based on type
            if (type == "tile_based_aie_tile_metrics") {
                collection.metrics.push_back(MetricEntry::processSettings(obj));
            } 
            else if(type == "graph_based_aie_tile_metrics") {
                collection.metrics.push_back(MetricEntry::processSettings(obj));
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
public:
    static MetricCollection parse(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) {
            throw std::runtime_error("Error opening file: " + filename);
        }

        boost::property_tree::ptree pt;
        boost::property_tree::read_json(file, pt);

        return MetricCollection::processSettings(pt);
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

// // Main function to demonstrate usage
// int main() {
//     // Example JSON input file
//     std::string jsonInput = R"([
//         { "type": "tile_based", "graph": "Graph1", "port": "Port1", "metric": "Metric1", "channel1": 10 },
//         { "type": "tile_based", "graph": "Graph2", "port": "Port2", "metric": "Metric2", "channel2": 20 }
//     ])";

//     // Write the JSON input to a file for demonstration
//     std::ofstream inputFile("xdp.json");
//     inputFile << jsonInput;
//     inputFile.close();

//     // Parse the JSON file
//     MetricCollection collection = JsonParser::parse("xdp.json");

//     // Print metrics
//     for (const auto& metric : collection.metrics) {
//         boost::property_tree::ptree pt = metric->toPtree();
//         boost::property_tree::write_json(std::cout, pt);
//     }

//     // Write the metrics back to a file
//     JsonParser::write("output.json", collection);

//     return 0;
// }

#endif