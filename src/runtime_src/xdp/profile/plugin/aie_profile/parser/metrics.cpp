// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved

#include "metrics.h"

namespace xdp {

    std::vector<uint8_t>
    Metric::getStartTile() const {
      return {}; // Default implementation for base class
    }
  
    std::vector<uint8_t>
    Metric::getEndTile() const {
      return {}; // Default implementation for base class
    }
  
    void 
    Metric::print() const {
      std::cout << "Metric: " << metric;
      if (channel0) {
          std::cout << ", Channel 1: " << *channel0;
      }
      if (channel1) {
          std::cout << ", Channel 2: " << *channel1;
      }
      std::cout << std::endl;
    }
  
  bool
  Metric::areChannelsSet() const {
    return (channel0.has_value() && channel1.has_value());
  }

  int
  Metric::getChannel0() const {
    if (channel0.has_value()) {
      return *channel0;
    }
    return -1; // or throw an exception
  }

  int 
  Metric::getChannel1() const {
    if (channel1.has_value()) {
      return *channel1;
    }
    return -1; // or throw an exception
  }

  std::string
  Metric::getBytesToTransfer() const {
    if (bytes_to_transfer.has_value()) {
      return *bytes_to_transfer;
    }
    return ""; // or throw an exception
  }

  Metric::Metric(std::string metric, std::optional<int> ch0,
                 std::optional<int> ch1, 
                 std::optional<std::string> bytes): metric(std::move(metric)), 
                 channel0(ch0), channel1(ch1), bytes_to_transfer(bytes) {}

  // Add common fields to ptree
  void
  Metric::addCommonFields(boost::property_tree::ptree& obj) const {
        obj.put("metric", metric);
        if (channel0) obj.put("ch0", *channel0);
        if (channel1) obj.put("ch1", *channel1);
        if (bytes_to_transfer) obj.put("bytes", *bytes_to_transfer);
    }

// --------------------------------------------------------------------------------------------------------------------
// GraphBasedMetricEntry class Definitions
    // Constructor
    GraphBasedMetricEntry::GraphBasedMetricEntry(std::string graph, std::string port, std::string metric, 
                          std::optional<int> ch0, std::optional<int> ch1, std::optional<std::string> bytes)
        : Metric(std::move(metric), ch0, ch1, bytes), graph(std::move(graph)), port(std::move(port)) {}

    // Convert to ptree
    boost::property_tree::ptree
    GraphBasedMetricEntry::toPtree() const {
        boost::property_tree::ptree obj;
        obj.put("graph", graph);
        obj.put("port", port);
        addCommonFields(obj);
        return obj;
    }

    // Create from ptree
    std::unique_ptr<Metric>
    GraphBasedMetricEntry::processSettings(const boost::property_tree::ptree& obj) {
        return std::make_unique<GraphBasedMetricEntry>(
            obj.get<std::string>("graph", "all"),
            obj.get<std::string>("port", "all"),
            obj.get<std::string>("metric", ""),
            obj.get_optional<int>("ch0") ? std::make_optional<int>(obj.get<int>("ch0")) : std::nullopt,
            obj.get_optional<int>("ch1") ? std::make_optional<int>(obj.get<int>("ch1")) : std::nullopt,
            obj.get_optional<std::string>("bytes") ? std::make_optional(obj.get<std::string>("bytes")) : std::nullopt
        );
    }

    void
    GraphBasedMetricEntry::print() const {
        std::cout << "^^^ print GraphBasedMetricEntry: " << graph << ", Port: " << port;
        Metric::print(); // Call the base class print method to show common fields
    }

// --------------------------------------------------------------------------------------------------------------------
// TileBasedMetricEntry class Definitions

    // Constructor
    TileBasedMetricEntry::TileBasedMetricEntry(std::vector<uint8_t> startTile, std::vector<uint8_t> endTile, std::string metric, 
                         std::optional<int> ch0, std::optional<int> ch1, std::optional<std::string> bytes)
        : Metric(std::move(metric), ch0, ch1, bytes), startTile(std::move(startTile)), endTile(std::move(endTile)) {}

    // Convert to ptree
    boost::property_tree::ptree
    TileBasedMetricEntry::toPtree() const {
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
    std::unique_ptr<Metric>
    TileBasedMetricEntry::processSettings(const boost::property_tree::ptree& obj) {
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
            obj.get<std::string>("metric", "NA"),
            obj.get_optional<int>("ch0") ? std::make_optional<int>(obj.get<int>("ch0")) : std::nullopt,
            obj.get_optional<int>("ch1") ? std::make_optional<int>(obj.get<int>("ch1")) : std::nullopt,
            obj.get_optional<std::string>("bytes") ? std::make_optional(obj.get<std::string>("bytes")) : std::nullopt
        );
    }

    std::vector<uint8_t>
    TileBasedMetricEntry::getStartTile() const {
      std::cout << "!!! TileBasedMetricEntry::getStartTile(): ";
      return startTile;
    }

    std::vector<uint8_t>
    TileBasedMetricEntry::getEndTile() const {
      std::cout << "!!! TileBasedMetricEntry::getEndTile(): ";
      return endTile;
    }

    void
    TileBasedMetricEntry::print() const {
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