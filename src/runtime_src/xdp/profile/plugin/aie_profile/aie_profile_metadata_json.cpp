// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved

#define XDP_PLUGIN_SOURCE

#include "aie_profile_metadata.h"

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "core/common/config_reader.h"
#include "core/common/device.h"
#include "core/common/message.h"
#include "xdp/profile/database/database.h"
#include "xdp/profile/plugin/vp_base/vp_base_plugin.h"
#include "xdp/profile/plugin/aie_profile/parser/metrics.h"

namespace xdp {
  using severity_level = xrt_core::message::severity_level;
  namespace pt = boost::property_tree;

  /****************************************************************************
   * Resolve metrics for AIE or Memory tiles
   ***************************************************************************/
  void AieProfileMetadata::getConfigMetricsForTilesUsingJson(const int moduleIdx, 
      const std::vector<std::string>& metricsSettings,
      const std::vector<std::string>& graphMetricsSettings, const module_type mod,
      MetricsCollectionManager& metricsCollectionManager)
  {
    // if ((metricsSettings.empty()) && (graphMetricsSettings.empty()))
    //   return;
    std::string metricSettingsName = "tile_based_" + moduleNames[moduleIdx] + "_metrics";

    const std::vector<std::unique_ptr<Metric>>* metrics = nullptr;
    try {
      const MetricCollection& tilesMetricCollection = metricsCollectionManager.getMetricCollection(mod, metricSettingsName);
      const auto& metrics = tilesMetricCollection.metrics;

    if (metrics.empty()) {
      xrt_core::message::send(severity_level::debug, "XRT",
                              "No metric collection found for " + metricSettingsName);
      return;
    }

    if ((metadataReader->getHardwareGeneration() == 1) && (mod == module_type::mem_tile)) {
      xrt_core::message::send(severity_level::warning, "XRT",
                              "Memory tiles are not available in AIE1. Profile "
                              "settings will be ignored.");
      return;
    }
    
    uint8_t rowOffset     = (mod == module_type::mem_tile) ? 1 : metadataReader->getAIETileRowOffset();
    std::string entryName = (mod == module_type::mem_tile) ? "buffer" : "kernel";
    std::string modName   = (mod == module_type::core) ? "aie" 
                          : ((mod == module_type::dma) ? "aie_memory" : "memory_tile");

    auto allValidGraphs  = metadataReader->getValidGraphs();
    std::vector<std::string> allValidEntries = (mod == module_type::mem_tile) ?
      metadataReader->getValidBuffers() : metadataReader->getValidKernels();

    std::set<tile_type> allValidTiles;
    auto validTilesVec = metadataReader->getTiles("all", mod, "all");
    std::unique_copy(validTilesVec.begin(), validTilesVec.end(), std::inserter(allValidTiles, allValidTiles.end()),
                     xdp::aie::tileCompare);

    // Process only range of tiles metric setting
    for (size_t i = 0; i < metrics.size(); ++i) {

      uint8_t minRow = 0, minCol = 0;
      uint8_t maxRow = 0, maxCol = 0;

      try {
        std::vector<uint8_t> minTile;
        minTile = metrics[i]->getStartTile();

        std::vector<uint8_t> maxTile;
        maxTile = metrics[i]->getEndTile();
        if (maxTile.empty())
          maxTile = minTile;
        
        if (minTile.empty() || maxTile.empty()) {
          std::stringstream msg;
          msg << "Tile range specification in tile_based_" << modName
              << "_metrics is not a valid format and hence skipped. Should be {<mincolumn,<minrow>}:{<maxcolumn>,<maxrow>}";
          xrt_core::message::send(severity_level::warning, "XRT", msg.str());
          continue;
        }
              
        minCol = minTile[0];
        minRow = minTile[1] + rowOffset;

        maxCol = maxTile[0];
        maxRow = maxTile[1] + rowOffset;
      }
      catch (...) {
        xrt_core::message::send(severity_level::warning, "XRT",
                                "Tile range specification in tile_based_" + modName
                                + "_metrics is not valid format and hence skipped.");
        continue;
      }

      // Ensure range is valid
      if ((minCol > maxCol) || (minRow > maxRow)) {
        std::stringstream msg;
        msg << "Tile range specification in tile_based_" << modName
            << "_metrics is not a valid range ({col1,row1}<={col2,row2}) and hence skipped.";
        xrt_core::message::send(severity_level::warning, "XRT", msg.str());
        continue;
      }

      uint8_t channel0 = 0;
      uint8_t channel1 = 1;

      if (metrics[i]->areChannelsSet()) {
        try {
          channel0 = metrics[i]->getChannel0();
          channel1 = metrics[i]->getChannel1();
        }
        catch (...) {
          std::stringstream msg;
          msg << "Channel specifications in tile_based_" << modName 
              << "_metrics are not valid and hence ignored.";
          xrt_core::message::send(severity_level::warning, "XRT", msg.str());
        }
      }

      for (uint8_t col = minCol; col <= maxCol; ++col) {
        for (uint8_t row = minRow; row <= maxRow; ++row) {
          tile_type tile;
          tile.col = col;
          tile.row = row;
          tile.active_core   = true;
          tile.active_memory = true;

          // Make sure tile is used
          auto it = std::find_if(allValidTiles.begin(), allValidTiles.end(),
            compareTileByLoc(tile));
          if (it == allValidTiles.end()) {
            std::stringstream msg;
            msg << "Specified Tile (" << std::to_string(tile.col) << "," 
                << std::to_string(tile.row) << ") is not active. Hence skipped.";
            xrt_core::message::send(severity_level::warning, "XRT", msg.str());
            continue;
          }

          configMetrics[moduleIdx][tile] = metrics[i]->metric;

          // Grab channel numbers (if specified; memory tiles only)
          if (metrics[i]->areChannelsSet()) {
            configChannel0[tile] = channel0;
            configChannel1[tile] = channel1;
          }
        }
      }
    } // End of metrics loop

    // Set default, check validity, and remove "off" tiles
    auto defaultSet = defaultSets[moduleIdx];
    bool showWarning = true;
    std::vector<tile_type> offTiles;

    for (auto& tileMetric : configMetrics[moduleIdx]) {
      auto tile = tileMetric.first;
      auto metricSet = tileMetric.second;

      // Save list of "off" tiles
      if (metricSet.empty() || (metricSet.compare("off") == 0)) {
        offTiles.push_back(tile);
        continue;
      }

      // Ensure requested metric set is supported (if not, use default)
      if (std::find(metricStrings.at(mod).begin(), metricStrings.at(mod).end(), metricSet) ==
          metricStrings.at(mod).end()) {
        if (showWarning) {
          std::stringstream msg;
          msg << "Unable to find " << moduleNames[moduleIdx] << " metric set " << metricSet
              << ". Using default of " << defaultSet << ".";
          xrt_core::message::send(severity_level::warning, "XRT", msg.str());
          showWarning = false;
        }

        tileMetric.second = defaultSet;
        metricSet = defaultSet;
      }

      // Specify complementary metric sets (as needed)
      // NOTE 1: Issue warning when we replace their setting
      // NOTE 2: This is agnostic to order and which setting is specified
      auto pairModuleIdx = getPairModuleIndex(metricSet, mod);
      if (pairModuleIdx >= 0) {
        auto pairItr = std::find_if(configMetrics[pairModuleIdx].begin(), 
          configMetrics[pairModuleIdx].end(), compareTileByLocMap(tile));

        if ((pairItr != configMetrics[pairModuleIdx].end())
            && (pairItr->second != metricSet)) {
          std::stringstream msg;
          msg << "Replacing metric set " << pairItr->second << " with complementary set " 
              << metricSet << " for tile (" << std::to_string(tile.col) << ","
              << std::to_string(tile.row) << ") [1].";
          xrt_core::message::send(severity_level::warning, "XRT", msg.str());
        }

        configMetrics[pairModuleIdx][tile] = metricSet;
        // Protect this setting by adding it to secondary map
        pairConfigMetrics[tile] = metricSet;
      }
      else {
        // Check if this tile/module was previously protected
        auto pairItr2 = std::find_if(pairConfigMetrics.begin(), 
          pairConfigMetrics.end(), compareTileByLocMap(tile));

        if (pairItr2 != pairConfigMetrics.end()
            && (pairItr2->second != metricSet)) {
          std::stringstream msg;
          msg << "Replacing metric set " << metricSet << " with complementary set " 
              << pairItr2->second << " for tile (" << std::to_string(tile.col) << ","
              << std::to_string(tile.row) << ") [2].";
          xrt_core::message::send(severity_level::warning, "XRT", msg.str());
          configMetrics[moduleIdx][tile] = pairItr2->second;
        }
      }
    }

    // Remove all the "off" tiles
    for (auto& t : offTiles) {
      configMetrics[moduleIdx].erase(t);
    }
  } catch (const std::exception& e) {
      xrt_core::message::send(severity_level::error, "XRT", e.what());
      return;
    }
  }


  void AieProfileMetadata::getConfigMetricsForInterfaceTilesUsingJson(const int moduleIdx,
      const std::vector<std::string>& metricsSettings,
      const std::vector<std::string> graphMetricsSettings,
      MetricsCollectionManager& metricsCollectionManager)
    {
      // if ((metricsSettings.empty()) && (graphMetricsSettings.empty()))
      //   return;
      std::string metricSettingsName = "tile_based_" + moduleNames[moduleIdx] + "_metrics";
      try {
        const MetricCollection& tilesMetricCollection = metricsCollectionManager.getMetricCollection(module_type::shim, metricSettingsName);
        const auto& metrics = tilesMetricCollection.metrics;
        if (metrics.empty()) {
          xrt_core::message::send(severity_level::debug, "XRT",
                                  "No metric collection found for " + metricSettingsName);
          return;
        }
      // auto & shimMetricCollection = metricsCollectionManager.getMetricCollection(module_type::shim, "tile_based_interface_tile_metrics");

      auto allValidGraphs = metadataReader->getValidGraphs();
      auto allValidPorts  = metadataReader->getValidPorts();

      // Pass 3 : process only single tile metric setting
      for (size_t i = 0; i < metrics.size(); ++i) {
        if (!isSupported(metrics[i]->metric, true))
          continue;

        uint8_t col = metrics[i]->getStartTile().front();
        std::cout << "!!! Shim Column: " << std::to_string(col) << std::endl;

            // xrt_core::message::send(severity_level::warning, "XRT",
            //                         "Column specification in tile_based_interface_tile_metrics "
            //                         "is not an integer and hence skipped.");
            // continue;

          // By-default select both the channels
          bool foundChannels = false;
          uint8_t channelId0 = 0;
          uint8_t channelId1 = 1;
          uint32_t bytes = defaultTransferBytes;
          //TODO: Support for user specified bytes.
            // if (profileAPIMetricSet(metrics[i][1])) {
            //   bytes = processUserSpecifiedBytes(metrics[i][2]);
            // }

            // else {
            //   try {
            //     foundChannels = true;
            //     channelId0 = aie::convertStringToUint8(metrics[i][2]);
            //     channelId1 = (metrics[i].size() == 3) ? channelId0 : aie::convertStringToUint8(metrics[i][3]);
            //   }
            //   catch (std::invalid_argument const&) {
            //     // Expected channel Id is not an integer, give warning and ignore
            //     foundChannels = false;
            //     xrt_core::message::send(severity_level::warning, "XRT", "Channel ID specification "
            //       "in tile_based_interface_tile_metrics is not an integer and hence ignored.");
            //   }
            // }

          foundChannels = metrics[i]->areChannelsSet();
          int16_t channelNum = (foundChannels) ? metrics[i]->getChannel0() : -1;
          auto tiles = metadataReader->getInterfaceTiles("all", "all", metrics[i]->metric, channelNum, true, col, col);

          std::cout << "!!! Total tiles: " << tiles.size() << std::endl;
          for (auto& t : tiles) {
            std::cout << "\t !!! Tile: (" << std::to_string(t.col) << ","
                      << std::to_string(t.row) << ")" << std::endl;
            std::cout << t << std::endl;
            configMetrics[moduleIdx][t] = metrics[i]->metric;
            configChannel0[t] = channelId0;
            configChannel1[t] = channelId1;
            if (metrics[i]->metric == METRIC_BYTE_COUNT) {
              std::string bytes_str = metrics[i]->getBytesToTransfer();
              uint32_t bytes = processUserSpecifiedBytes(bytes_str);
              if (bytes > 0)
                setUserSpecifiedBytes(t, bytes);
              else
                xrt_core::message::send(severity_level::warning, "XRT", "User specified bytes is not set or non-zero.");
            }
          }
        }
      
        // Set default, check validity, and remove "off" tiles
        auto defaultSet = defaultSets[moduleIdx];
        bool showWarning = true;
        std::vector<tile_type> offTiles;
        auto metricVec = metricStrings.at(module_type::shim);

        for (auto& tileMetric : configMetrics[moduleIdx]) {
          // Save list of "off" tiles
          if (tileMetric.second.empty() || (tileMetric.second.compare("off") == 0)) {
            offTiles.push_back(tileMetric.first);
            continue;
          }

          // Ensure requested metric set is supported (if not, use default)
          if (std::find(metricVec.begin(), metricVec.end(), tileMetric.second) == metricVec.end()) {
            if (showWarning) {
              std::string msg = "Unable to find interface_tile metric set " + tileMetric.second
                                + ". Using default of " + defaultSet + ". ";
              xrt_core::message::send(severity_level::warning, "XRT", msg);
              showWarning = false;
            }

            tileMetric.second = defaultSet;
          }
        }

        // Remove all the "off" tiles
        for (auto& t : offTiles) {
          configMetrics[moduleIdx].erase(t);
        }

        // Print configMetrics for moduleIdx.
        for (auto & tileMetric : configMetrics[moduleIdx]) {
          auto tile = tileMetric.first;
          auto metricSet = tileMetric.second;
          std::cout << "!!! Module Index: " << moduleIdx << ", Tile: (" << std::to_string(tile.col) << ","
                    << std::to_string(tile.row) << "), Metric Set: " << metricSet << std::endl;
        }
      } catch (const std::exception& e) {
        xrt_core::message::send(severity_level::error, "XRT", e.what());
        return;
      }
    }

}  // namespace xdp
