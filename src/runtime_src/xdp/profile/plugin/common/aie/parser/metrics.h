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
#include "xdp/profile/database/static_info/aie_constructs.h"
#include "xdp/profile/plugin/common/aie/parser/parser_utils.h"
#include "xdp/profile/plugin/common/aie/parser/metrics_type.h"

namespace xdp {
  namespace pt = boost::property_tree;
  using severity_level = xrt_core::message::severity_level;

  inline metric_type getMetricTypeFromKey(const std::string& settingsKey, const std::string& key) {
    if (settingsKey == "tiles") {
      if (key == "aie_tile")        return metric_type::TILE_BASED_AIE_TILE;
      if (key == "aie")             return metric_type::TILE_BASED_CORE_MOD;
      if (key == "aie_memory")      return metric_type::TILE_BASED_MEM_MOD;
      if (key == "interface_tile")  return metric_type::TILE_BASED_INTERFACE_TILE;
      if (key == "memory_tile")     return metric_type::TILE_BASED_MEM_TILE;
      if (key == "microcontroller") return metric_type::TILE_BASED_UC;
    } else if (settingsKey == "graphs") {
      if (key == "aie_tile")        return metric_type::GRAPH_BASED_AIE_TILE;
      if (key == "aie")             return metric_type::GRAPH_BASED_CORE_MOD;
      if (key == "aie_memory")      return metric_type::GRAPH_BASED_MEM_MOD;
      if (key == "interface_tile")  return metric_type::GRAPH_BASED_INTERFACE_TILE;
      if (key == "memory_tile")     return metric_type::GRAPH_BASED_MEM_TILE;
    }
    return metric_type::NUM_TYPES;
  }

  inline module_type getModuleTypeFromKey(const std::string& key) {
    static const std::map<std::string, module_type> keyToModuleType = {
        {"aie",             module_type::core},
        {"aie_memory",      module_type::dma},
        {"interface_tile",  module_type::shim},
        {"memory_tile",     module_type::mem_tile},
        {"microcontroller", module_type::uc}
    };

    auto it = keyToModuleType.find(key);
    return (it != keyToModuleType.end()) ? it->second : module_type::num_types;
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
      virtual uint8_t getCol() const = 0;
      virtual uint8_t getRow() const = 0;
      virtual void setAllTilesRange(bool allTiles) = 0;
      virtual bool isAllTilesRangeSet() const = 0;
      virtual void setTilesRange(bool tileRange) = 0;
      virtual bool isTilesRangeSet() const = 0;
    
      virtual void print() const;
      // Convert metric to ptree
      virtual boost::property_tree::ptree toPtree() const = 0;
  public:
    std::string metric;
    std::optional<std::vector<uint8_t>> channels;
    std::optional<std::string> bytes_to_transfer;

    bool areChannelsSet() const;
    bool isChannel0Set() const;
    bool isChannel1Set() const;
    int getChannel0() const;
    int getChannel1() const;
    std::string getBytesToTransfer() const;

    Metric(std::string metric, std::optional<std::vector<uint8_t>> ch = std::nullopt, 
           std::optional<std::string> bytes = std::nullopt);
    // Add common fields to ptree
    void addCommonFields(boost::property_tree::ptree& obj) const;
  };

  // GraphBasedMetricEntry class
  class GraphBasedMetricEntry : public Metric {
  public:
      std::string graph;
      std::string entity;
      uint8_t col, row;
      bool allTilesRange = false;
      bool tileRange = false;

      // Constructor
      GraphBasedMetricEntry(std::string graph, std::string entity, std::string metric, 
                            std::optional<std::vector<uint8_t>> ch = std::nullopt, std::optional<std::string> bytes = std::nullopt);
      // Create from ptree
      static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
      uint8_t getCol() const override { return col; }
      uint8_t getRow() const override { return row; }
      void setAllTilesRange(bool allTiles) override { allTilesRange = allTiles; }
      bool isAllTilesRangeSet() const { return allTilesRange; }
      virtual void setTilesRange(bool tileRange) override { tileRange = tileRange; }
      virtual bool isTilesRangeSet() const { return tileRange; }
      
      // Debug Methods
      boost::property_tree::ptree toPtree() const override;
      void print() const;
  };

  // TileBasedMetricEntry class
  class TileBasedMetricEntry : public Metric {
  public:
      std::vector<uint8_t> startTile;
      std::vector<uint8_t> endTile;
      uint8_t col, row;
      bool allTilesRange = false;
      bool tileRange = false;

      // Constructor based on column and row per tile
      TileBasedMetricEntry(uint8_t col, uint8_t row, std::string metric,
                           std::optional<std::vector<uint8_t>> ch = std::nullopt, std::optional<std::string> bytes = std::nullopt);

      // Constructor based on start and end tiles range or "all" tiles
      TileBasedMetricEntry(std::vector<uint8_t> startTile, std::vector<uint8_t> endTile, std::string metric, 
                           std::optional<std::vector<uint8_t>> ch = std::nullopt, std::optional<std::string> bytes = std::nullopt);
      // Create from ptree
      static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
      std::vector<uint8_t> getStartTile() const override;
      std::vector<uint8_t> getEndTile() const override;
      uint8_t getCol() const override { return col; }
      uint8_t getRow() const override { return row; }

      void setAllTilesRange(bool allTiles) override { allTilesRange = allTiles; }
      bool isAllTilesRangeSet() const { return allTilesRange; }
      virtual void setTilesRange(bool tileRange) override { tileRange = tileRange; }
      virtual bool isTilesRangeSet() const { return tileRange; }
 
      // Debug Methods
      boost::property_tree::ptree toPtree() const override;
      void print() const;
  };

}

#endif