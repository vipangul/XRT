// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved
#ifndef XDP_METRICS_COLLECTION_H
#define XDP_METRICS_COLLECTION_H

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
#include "metrics.h"

namespace xdp {
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

      // static MetricCollection processSettings(const boost::property_tree::ptree& ptArr, 
      //                                         metric_type type);
      
      void addMetric(std::unique_ptr<Metric> metric);
      bool hasAllTileRanges() const;
      bool hasIndividualTiles() const;

      // Convert to ptree array
      boost::property_tree::ptree toPtree() const;
      void print() const;
  }; // class MetricCollection
}; // namespace xdp
#endif // XDP_METRICS_COLLECTION_H