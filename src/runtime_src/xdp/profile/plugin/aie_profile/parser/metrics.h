// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved
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
#include "metrics_type.h"

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
      // Create a metric from ptree
      static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
      virtual std::vector<uint8_t> getStartTile() const;
      virtual std::vector<uint8_t> getEndTile() const;
    
      virtual void print() const;
      // Convert metric to ptree
      virtual boost::property_tree::ptree toPtree() const = 0;
  public:
    std::string metric;
    std::optional<int> channel0;
    std::optional<int> channel1;
    std::optional<std::string> bytes_to_transfer;

    bool areChannelsSet() const;
    int getChannel0() const;
    int getChannel1() const;
    std::string getBytesToTransfer() const;
    Metric(std::string metric, std::optional<int> ch0 = std::nullopt, std::optional<int> ch1 = std::nullopt, 
            std::optional<std::string> bytes = std::nullopt);
    // Add common fields to ptree
    void addCommonFields(boost::property_tree::ptree& obj) const;
  };

  // GraphBasedMetricEntry class
  class GraphBasedMetricEntry : public Metric {
  public:
      std::string graph;
      std::string port;

      // Constructor
      GraphBasedMetricEntry(std::string graph, std::string port, std::string metric, 
                            std::optional<int> ch0 = std::nullopt, std::optional<int> ch1 = std::nullopt, std::optional<std::string> bytes = std::nullopt);
      // Create from ptree
      static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
      
      // Convert to ptree
      boost::property_tree::ptree toPtree() const override;
      void print() const;
  };

  // TileBasedMetricEntry class
  class TileBasedMetricEntry : public Metric {
  public:
      std::vector<uint8_t> startTile;
      std::vector<uint8_t> endTile;

      // Constructor
      TileBasedMetricEntry(std::vector<uint8_t> startTile, std::vector<uint8_t> endTile, std::string metric, 
                          std::optional<int> ch0 = std::nullopt, std::optional<int> ch1 = std::nullopt, std::optional<std::string> bytes = std::nullopt);
      // Create from ptree
      static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
      std::vector<uint8_t> getStartTile() const override;
      std::vector<uint8_t> getEndTile() const override;
      
      // Convert to ptree
      boost::property_tree::ptree toPtree() const override;
      void print() const;
  };

}

#endif