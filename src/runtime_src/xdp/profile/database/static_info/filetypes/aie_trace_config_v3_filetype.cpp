// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved

#define XDP_CORE_SOURCE

#include "aie_trace_config_v3_filetype.h"
#include "core/common/message.h"
#include "xdp/profile/database/static_info/aie_util.h"
#include "xdp/profile/plugin/vp_base/utility.h"

#include <boost/algorithm/string.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>

namespace xdp::aie {
namespace pt = boost::property_tree;
using severity_level = xrt_core::message::severity_level;

AIETraceConfigV3Filetype::AIETraceConfigV3Filetype(boost::property_tree::ptree& aie_project)
: AIETraceConfigFiletype(aie_project) {}

std::vector<std::string>
AIETraceConfigV3Filetype::getValidKernels() const
{
    std::vector<std::string> kernels;

    // Grab all kernel to tile mappings
    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::vector<std::string> functions;
        std::string functionStr = mapping.second.get<std::string>("function");
        if (functionStr.empty())
            continue; // Skip empty function names

        boost::split(functions, functionStr, boost::is_any_of(" "));
        for (auto& function : functions) {
            std::vector<std::string> names;
            boost::split(names, function, boost::is_any_of("."));
            std::unique_copy(names.begin(), names.end(), std::back_inserter(kernels));
        }

        // TODO: code review this usecase
        // Store the complete function name
        // e.g. "graph_name.kernel_name" or "graph_name.kernel_name.sub_kernel
        kernels.push_back(functionStr);
    }
    return kernels;
}

// Find all AIE or memory tiles associated with a graph and kernel/buffer
//   kernel_name = all      : all tiles in graph
//   kernel_name = <kernel> : only tiles used by that specific kernel
std::vector<tile_type>
AIETraceConfigV3Filetype::getTiles(const std::string& graph_name,
                                 module_type type,
                                 const std::string& kernel_name) const
{
    bool isAllGraph  = (graph_name.compare("all") == 0);
    bool isAllKernel = (kernel_name.compare("all") == 0);

    if (type == module_type::mem_tile)
        return getMemoryTiles(graph_name, kernel_name);
    if (isAllKernel)
        return getAllAIETiles(graph_name);

    // Now search by graph-kernel pairs
    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping && isAllKernel)
        return getAIETiles(graph_name);
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    std::vector<tile_type> tiles;
    auto rowOffset = getAIETileRowOffset();

    // Parse all kernel mappings
    for (auto const &mapping : kernelToTileMapping.get()) {
        bool foundGraph  = isAllGraph;
        bool foundKernel = isAllKernel;

        if (!foundGraph || !foundKernel) {
            auto graphStr = mapping.second.get<std::string>("graph");
            std::vector<std::string> graphs;
            boost::split(graphs, graphStr, boost::is_any_of(" "));

            auto functionStr = mapping.second.get<std::string>("function");
            std::vector<std::string> functions;
            boost::split(functions, functionStr, boost::is_any_of(" "));

            // Verify this entry has desired graph/kernel combo
            for (uint32_t i=0; i < std::min(graphs.size(), functions.size()); ++i) {
                foundGraph  |= (graphs.at(i).find(graph_name) != std::string::npos);

                std::vector<std::string> names;
                boost::split(names, functions.at(i), boost::is_any_of("."));
                if (std::find(names.begin(), names.end(), kernel_name) != names.end())
                    foundKernel = true;

                if (foundGraph && foundKernel)
                    break;
            }
        }

        // Add to list if verified
        if (foundGraph && foundKernel) {
            tile_type tile;
            tile.col = mapping.second.get<uint8_t>("column");
            tile.row = mapping.second.get<uint8_t>("row") + rowOffset;
            tile.active_core = true;
            tile.active_memory = true;
            tiles.emplace_back(std::move(tile));
        }
    }
    return tiles;
}

// Find all AIE tiles in a graph that use core and/or memories (kernel_name = all)
std::vector<tile_type>
AIETraceConfigV3Filetype::getAllAIETiles(const std::string& graph_name) const
{
    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    std::vector<tile_type> tiles;
    auto rowOffset = getAIETileRowOffset();
    int count = 0;

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::vector<std::string> graphs;
        std::string graphStr = mapping.second.get<std::string>("graph");
        if (graphStr.empty())
            continue; // Skip empty graph names
        
        if ((graphStr.find(graph_name) == std::string::npos) && (graph_name.compare("all")) != 0)
            continue; // Skip graphs/subgraphs that do not match the requested graph name

        tiles.push_back(tile_type());
        auto& t = tiles.at(count++);
        t.col = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("column"));
        t.row = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("row")) + rowOffset;
        t.active_core = mapping.second.get<bool>("isCoreUsed", true);
        t.active_memory = mapping.second.get<bool>("isDMAUsed", true);
    }
 
    return tiles;
}

// Find all AIE tiles in a graph that use the core (kernel_name = all)
std::vector<tile_type> 
AIETraceConfigV3Filetype::getAIETiles(const std::string& graph_name) const
{   
    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    std::vector<tile_type> tiles;
    auto rowOffset = getAIETileRowOffset();
    int count = 0;

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::vector<std::string> graphs;
        std::string graphStr = mapping.second.get<std::string>("graph");
        if (graphStr.empty())
            continue; // Skip empty graph names
        
        if ((graphStr.find(graph_name) == std::string::npos) && (graph_name.compare("all")) != 0)
            continue; // Skip graphs/subgraphs that do not match the requested graph name

        // Check if isCoreUsed is set to false
        if (!mapping.second.get<bool>("isCoreUsed", true))
            continue; // Skip tiles that do not use the core

        tiles.push_back(tile_type());
        auto& t = tiles.at(count++);
        t.col = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("column"));
        t.row = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("row")) + rowOffset;
        t.active_core = mapping.second.get<bool>("isCoreUsed", true);
        t.active_memory = mapping.second.get<bool>("isDMAUsed", true);
    }
 
    return tiles;
}

// Find all AIE tiles in a graph that use the core or memory module (kernels = all)
std::vector<tile_type>
AIETraceConfigV3Filetype::getEventTiles(const std::string& graph_name,
                                        module_type type) const
{
    if ((type == module_type::shim) || (type == module_type::mem_tile))
        return {};

    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    std::vector<tile_type> tiles;
    auto rowOffset = getAIETileRowOffset();
    int count = 0;
    const char* eventTileKey = (type == module_type::core) ? "isCoreUsed" : "isDMAUsed";

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::vector<std::string> graphs;
        std::string graphStr = mapping.second.get<std::string>("graph");
        if (graphStr.empty())
            continue; // Skip empty graph names
        
        if ((graphStr.find(graph_name) == std::string::npos) && (graph_name.compare("all")) != 0)
            continue; // Skip graphs/subgraphs that do not match the requested graph name

        // skip tiles that do not use the eventTileKey ie core or memory
        if (!mapping.second.get<bool>(eventTileKey, false))
            continue;

        tiles.push_back(tile_type());
        auto& t = tiles.at(count++);
        t.col = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("column"));
        t.row = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("row")) + rowOffset;
        t.active_core = mapping.second.get<bool>("isCoreUsed", true);
        t.active_memory = mapping.second.get<bool>("isDMAUsed", true);
    }
 
    return tiles;
}

} // namespace xdp::aie
