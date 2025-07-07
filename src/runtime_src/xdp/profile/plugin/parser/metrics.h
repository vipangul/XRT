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
#include "xdp/profile/plugin/parser/parser_utils.h"
#include "xdp/profile/plugin/parser/metrics_type.h"

namespace xdp {
  namespace pt = boost::property_tree;
  using severity_level = xrt_core::message::severity_level;

  // --------------------------------------------------------------------------------------------------------------------
  // Base interface for all metrics
  class Metric {
  public:
      virtual ~Metric() = default;

      virtual void setAllTiles(bool allTiles) { allTilesRange = allTiles; }
      virtual bool isAllTilesSet() const      { return allTilesRange; }
      virtual void setTilesRange(bool range)  { tileRange = range; }
      virtual bool isTilesRangeSet() const    { return tileRange; }

      virtual std::string getGraph() const { return ""; }
      virtual std::string getGraphEntity() const { return ""; }

      virtual std::vector<uint8_t> getStartTile() const { return {}; }
      virtual std::vector<uint8_t> getEndTile() const { return {}; }
      virtual uint8_t getCol() const { return 0; }
      virtual uint8_t getRow() const { return 0; }

      virtual bool isGraphBased() const { return false; }
      virtual bool isTileBased() const { return false; }
 
      virtual void print() const;
      // Convert metric to ptree
      virtual boost::property_tree::ptree toPtree() const = 0;
  public:
    std::string metric;
    std::optional<std::vector<uint8_t>> channels;
    std::optional<std::string> bytes_to_transfer;
    bool allTilesRange = false;
    bool tileRange = false;

    const std::string& getMetric() const { return metric; }
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
      
      // Constructor
      GraphBasedMetricEntry(std::string graph, std::string entity, std::string metric, 
                            std::optional<std::vector<uint8_t>> ch = std::nullopt, std::optional<std::string> bytes = std::nullopt);
      // Create from ptree
      static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
      std::string getGraph() const override       { return graph; }
      std::string getGraphEntity() const override { return entity; }
      virtual bool isGraphBased() const override  { return true; }
      virtual bool isTileBased() const override   { return false; }
     
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

      // Constructor based on column and row per tile
      TileBasedMetricEntry(uint8_t col, uint8_t row, std::string metric,
                           std::optional<std::vector<uint8_t>> ch = std::nullopt, std::optional<std::string> bytes = std::nullopt);

      // Constructor based on start and end tiles range or "all" tiles
      TileBasedMetricEntry(std::vector<uint8_t> startTile, std::vector<uint8_t> endTile, std::string metric, 
                           std::optional<std::vector<uint8_t>> ch = std::nullopt, std::optional<std::string> bytes = std::nullopt);
      // Create from ptree
      static std::unique_ptr<Metric> processSettings(const boost::property_tree::ptree& obj);
      
      virtual std::vector<uint8_t> getStartTile() const override { return startTile; }
      virtual std::vector<uint8_t> getEndTile() const override   { return endTile; }
      virtual uint8_t getCol() const override { return col; }
      virtual uint8_t getRow() const override { return row; }
      
      virtual bool isGraphBased() const override { return false; }
      virtual bool isTileBased() const override  { return true; }
 
      // Debug Methods
      boost::property_tree::ptree toPtree() const override;
      void print() const;
  };

}

#endif