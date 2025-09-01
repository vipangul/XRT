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
#include <set>
#include <algorithm>

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

    std::set<std::string> uniqueKernels; // Use set to avoid duplicates

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::string functionStr = mapping.second.get<std::string>("function");
        if (functionStr.empty())
            continue; // Skip empty function names

        // Extract kernel names from function string
        std::vector<std::string> names;
        boost::split(names, functionStr, boost::is_any_of("."));
        
        // Add individual kernel components
        for (const auto& name : names) {
            if (!name.empty()) {
                uniqueKernels.insert(name);
            }
        }

        // Also store the complete function name
        uniqueKernels.insert(functionStr);
    }
    
    // Convert set to vector
    kernels.assign(uniqueKernels.begin(), uniqueKernels.end());
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
        
        // Compute isCoreUsed: true if tile type is "aie"
        std::string tileType = mapping.second.get<std::string>("tile", "");
        t.active_core = (tileType == "aie");
        
        // Compute isDMAUsed: true if tile has non-empty dmaChannels
        auto dmaChannelsTree = mapping.second.get_child_optional("dmaChannels");
        t.active_memory = (dmaChannelsTree && !dmaChannelsTree.get().empty());
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

        // Compute isCoreUsed: true if tile type is "aie"
        std::string tileType = mapping.second.get<std::string>("tile", "");
        bool isCoreUsed = (tileType == "aie");
        
        // Skip tiles that do not use the core
        if (!isCoreUsed)
            continue;

        tiles.push_back(tile_type());
        auto& t = tiles.at(count++);
        t.col = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("column"));
        t.row = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("row")) + rowOffset;
        t.active_core = isCoreUsed;
        
        // Compute isDMAUsed: true if tile has non-empty dmaChannels
        auto dmaChannelsTree = mapping.second.get_child_optional("dmaChannels");
        t.active_memory = (dmaChannelsTree && !dmaChannelsTree.get().empty());
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

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::vector<std::string> graphs;
        std::string graphStr = mapping.second.get<std::string>("graph");
        if (graphStr.empty())
            continue; // Skip empty graph names
        
        if ((graphStr.find(graph_name) == std::string::npos) && (graph_name.compare("all")) != 0)
            continue; // Skip graphs/subgraphs that do not match the requested graph name

        // Compute isCoreUsed: true if tile type is "aie"
        std::string tileType = mapping.second.get<std::string>("tile", "");
        bool isCoreUsed = (tileType == "aie");
        
        // Compute isDMAUsed: true if tile has non-empty dmaChannels
        auto dmaChannelsTree = mapping.second.get_child_optional("dmaChannels");
        bool isDMAUsed = (dmaChannelsTree && !dmaChannelsTree.get().empty());
        
        // Filter based on the requested module type
        bool includesTile = false;
        if (type == module_type::core) {
            includesTile = isCoreUsed;
        } else if (type == module_type::dma) {
            includesTile = isDMAUsed;
        }
        
        // Skip tiles that do not match the requested module type
        if (!includesTile)
            continue;

        tiles.push_back(tile_type());
        auto& t = tiles.at(count++);
        t.col = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("column"));
        t.row = xdp::aie::convertStringToUint8(mapping.second.get<std::string>("row")) + rowOffset;
        t.active_core = isCoreUsed;
        t.active_memory = isDMAUsed;
    }
 
    return tiles;
}

// Parse all tile mappings from metadata into aie_tile_info structures
std::vector<aie_tile_info>
AIETraceConfigV3Filetype::parseTileMappings() const
{
    std::vector<aie_tile_info> tileInfos;
    
    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    for (auto const &mapping : kernelToTileMapping.get()) {
        aie_tile_info tileInfo;
        
        tileInfo.graph = mapping.second.get<std::string>("graph", "");
        tileInfo.tile_type = mapping.second.get<std::string>("tile", "");
        tileInfo.column = mapping.second.get<uint8_t>("column");
        tileInfo.row = mapping.second.get<uint8_t>("row");
        tileInfo.schedule = mapping.second.get<uint8_t>("schedule", 0);
        tileInfo.function = mapping.second.get<std::string>("function", "");
        
        // Parse DMA channels
        auto dmaChannelsTree = mapping.second.get_child_optional("dmaChannels");
        if (dmaChannelsTree) {
            for (auto const &dmaChannel : dmaChannelsTree.get()) {
                dma_channel_type dmaInfo;
                dmaInfo.portName = dmaChannel.second.get<std::string>("portName", "");
                dmaInfo.column = dmaChannel.second.get<uint8_t>("column");
                dmaInfo.row = dmaChannel.second.get<uint8_t>("row");
                dmaInfo.channel = dmaChannel.second.get<uint8_t>("channel");
                dmaInfo.direction = dmaChannel.second.get<std::string>("direction", "");
                
                tileInfo.dmaChannels.push_back(dmaInfo);
            }
        }
        
        tileInfos.push_back(tileInfo);
    }
    
    return tileInfos;
}

// Get AIE tile information with optional filtering by graph and kernel
std::vector<aie_tile_info>
AIETraceConfigV3Filetype::getAIETileInfos(const std::string& graph_name,
                                           const std::string& kernel_name) const
{
    auto allTileInfos = parseTileMappings();
    
    if (graph_name == "all" && kernel_name == "all") {
        return allTileInfos;
    }
    
    std::vector<aie_tile_info> filteredTiles;
    
    for (const auto& tileInfo : allTileInfos) {
        bool matchesGraph = (graph_name == "all") || matchesGraphPattern(tileInfo.graph, graph_name);
        bool matchesKernel = (kernel_name == "all") || matchesFunctionPattern(tileInfo.function, kernel_name);
        
        if (matchesGraph && matchesKernel) {
            filteredTiles.push_back(tileInfo);
        }
    }
    
    return filteredTiles;
}

// Get DMA channels by column and row coordinates
std::vector<dma_channel_type>
AIETraceConfigV3Filetype::getDMAChannels(uint8_t column, uint8_t row) const
{
    std::vector<dma_channel_type> dmaChannels;
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        if (tileInfo.column == column && tileInfo.row == row) {
            for (const auto& dmaChannel : tileInfo.dmaChannels) {
                dmaChannels.push_back(dmaChannel);
            }
        }
    }
    
    return dmaChannels;
}

// Get DMA channels by port name
std::vector<dma_channel_type>
AIETraceConfigV3Filetype::getDMAChannelsByPortName(const std::string& portName) const
{
    std::vector<dma_channel_type> dmaChannels;
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        for (const auto& dmaChannel : tileInfo.dmaChannels) {
            if (dmaChannel.portName == portName) {
                dmaChannels.push_back(dmaChannel);
            }
        }
    }
    
    return dmaChannels;
}

// Get AIE tiles by tile type (e.g., "aie", "mem", etc.)
std::vector<aie_tile_info>
AIETraceConfigV3Filetype::getAIETilesByType(const std::string& tile_type) const
{
    std::vector<aie_tile_info> filteredTiles;
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        if (tile_type == "all" || tileInfo.tile_type == tile_type) {
            filteredTiles.push_back(tileInfo);
        }
    }
    
    return filteredTiles;
}

// Filter tiles by graph and function patterns
std::vector<aie_tile_info>
AIETraceConfigV3Filetype::filterTilesByGraphFunction(const std::string& graph_pattern,
                                                      const std::string& function_pattern) const
{
    std::vector<aie_tile_info> filteredTiles;
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        bool matchesGraph = matchesGraphPattern(tileInfo.graph, graph_pattern);
        bool matchesFunction = matchesFunctionPattern(tileInfo.function, function_pattern);
        
        if (matchesGraph && matchesFunction) {
            filteredTiles.push_back(tileInfo);
        }
    }
    
    return filteredTiles;
}

// Helper method to match graph patterns
bool AIETraceConfigV3Filetype::matchesGraphPattern(const std::string& graph, const std::string& pattern) const
{
    if (pattern == "all" || pattern.empty()) {
        return true;
    }
    
    // Support partial matching - graph contains pattern
    return graph.find(pattern) != std::string::npos;
}

// Helper method to match function patterns
bool AIETraceConfigV3Filetype::matchesFunctionPattern(const std::string& function, const std::string& pattern) const
{
    if (pattern == "all" || pattern.empty()) {
        return true;
    }
    
    // Support exact matching of function components
    std::vector<std::string> functionParts;
    boost::split(functionParts, function, boost::is_any_of("."));
    
    // Check if any part matches the pattern
    for (const auto& part : functionParts) {
        if (part == pattern || part.find(pattern) != std::string::npos) {
            return true;
        }
    }
    
    return false;
}

} // namespace xdp::aie
