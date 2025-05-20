// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved

#include "metrics_collection.h"

namespace xdp {
// MetricCollection class for managing a collection of metrics

    // Create from ptree array
    MetricCollection
    MetricCollection::processSettings(const boost::property_tree::ptree& ptArr, 
                                            metric_type type)
    {
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
    boost::property_tree::ptree
    MetricCollection::toPtree() const {
        boost::property_tree::ptree arr;
        for (const auto& metric : metrics) {
            metric->print();
            boost::property_tree::ptree obj = metric->toPtree();
            arr.push_back(std::make_pair("", obj));
        }
        return arr;
    }

    void
    MetricCollection::print() const {
        std::cout << "!!! Print MetricCollection:" << std::endl;
        for (const auto& metric : metrics) {
            if (metric) {
                metric->print();
            } else {
                xrt_core::message::send(severity_level::warning, "XRT", "Null metric found in collection");
            }
        }
    }
}; // namespace xdp