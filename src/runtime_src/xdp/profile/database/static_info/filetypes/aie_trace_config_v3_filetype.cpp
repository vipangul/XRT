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
AIETraceConfigV3Filetype::getValidGraphs() const
{
    std::vector<std::string> graphs;

    // Grab all kernel to tile mappings
    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    std::set<std::string> uniqueGraphs; // Use set to avoid duplicates

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::string graphStr = mapping.second.get<std::string>("graph");
        if (graphStr.empty())
            continue; // Skip empty graph names

        // Extract subgraph names from complete graph string
        std::vector<std::string> names;
        boost::split(names, graphStr, boost::is_any_of("."));
        
        // Add individual subgraph components
        for (const auto& name : names) {
            if (!name.empty()) {
                uniqueGraphs.insert(name);
            }
        }

        // Add the complete graph name
        uniqueGraphs.insert(graphStr);
    }
    
    // Convert set to vector
    graphs.assign(uniqueGraphs.begin(), uniqueGraphs.end());
    return graphs;
}

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
    if (type == module_type::mem_tile)
        return getMemoryTiles(graph_name, kernel_name);
    
    // For DMA type, we want tiles that have DMA channels (both core tiles and DMA-only)
    if (type == module_type::dma)
        return getEventTiles(graph_name, type);
    
    // For core type or default, get tiles that use cores
    if (kernel_name == "all")
        return getAllAIETiles(graph_name);
    
    // Now search by graph-kernel pairs for specific kernel
    auto kernelToTileMapping = aie_meta.get_child_optional("aie_metadata.TileMapping.AIEKernelToTileMapping");
    if (!kernelToTileMapping) {
        xrt_core::message::send(severity_level::info, "XRT", getMessage("TileMapping.AIEKernelToTileMapping"));
        return {};
    }

    std::vector<tile_type> tiles;
    auto rowOffset = getAIETileRowOffset();

    // Parse all kernel mappings
    for (auto const &mapping : kernelToTileMapping.get()) {
        std::string graphStr = mapping.second.get<std::string>("graph", "");
        std::string functionStr = mapping.second.get<std::string>("function", "");
        
        if (graphStr.empty() || functionStr.empty())
            continue;

        // Check if graph matches
        bool foundGraph = (graph_name == "all") || (graphStr.find(graph_name) != std::string::npos);
        if (!foundGraph)
            continue;

        // Check if kernel/function matches
        bool foundKernel = false;
        if (kernel_name == "all") {
            foundKernel = true;
        } else {
            // Check if kernel_name matches any part of the function
            std::vector<std::string> functionParts;
            boost::split(functionParts, functionStr, boost::is_any_of("."));
            
            for (const auto& part : functionParts) {
                if (part.find(kernel_name) != std::string::npos) {
                    foundKernel = true;
                    break;
                }
            }
            
            // Also check full function string
            if (!foundKernel && functionStr.find(kernel_name) != std::string::npos) {
                foundKernel = true;
            }
        }

        // Add tile if it matches the criteria
        if (foundGraph && foundKernel) {
            tile_type tile;
            tile.col = mapping.second.get<uint8_t>("column");
            tile.row = mapping.second.get<uint8_t>("row") + rowOffset;
            
            // Compute tile properties from metadata
            std::string tileType = mapping.second.get<std::string>("tile", "");
            tile.active_core = (tileType == "aie");
            
            auto dmaChannelsTree = mapping.second.get_child_optional("dmaChannels");
            tile.active_memory = (dmaChannelsTree && !dmaChannelsTree.get().empty());
            
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

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::string graphStr = mapping.second.get<std::string>("graph", "");
        if (graphStr.empty())
            continue; // Skip empty graph names
        
        if ((graphStr.find(graph_name) == std::string::npos) && (graph_name.compare("all") != 0))
            continue; // Skip graphs/subgraphs that do not match the requested graph name

        tile_type tile;
        tile.col = mapping.second.get<uint8_t>("column");
        tile.row = mapping.second.get<uint8_t>("row") + rowOffset;
        
        // Compute isCoreUsed: true if tile type is "aie"
        std::string tileType = mapping.second.get<std::string>("tile", "");
        tile.active_core = (tileType == "aie");
        
        // Compute isDMAUsed: true if tile has non-empty dmaChannels
        auto dmaChannelsTree = mapping.second.get_child_optional("dmaChannels");
        tile.active_memory = (dmaChannelsTree && !dmaChannelsTree.get().empty());
        
        tiles.push_back(tile);
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

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::string graphStr = mapping.second.get<std::string>("graph", "");
        if (graphStr.empty())
            continue; // Skip empty graph names
        
        if ((graphStr.find(graph_name) == std::string::npos) && (graph_name.compare("all") != 0))
            continue; // Skip graphs/subgraphs that do not match the requested graph name

        // Compute isCoreUsed: true if tile type is "aie"
        std::string tileType = mapping.second.get<std::string>("tile", "");
        bool isCoreUsed = (tileType == "aie");
        
        // Skip tiles that do not use the core
        if (!isCoreUsed)
            continue;

        tile_type tile;
        tile.col = mapping.second.get<uint8_t>("column");
        tile.row = mapping.second.get<uint8_t>("row") + rowOffset;
        tile.active_core = isCoreUsed;
        
        // Compute isDMAUsed: true if tile has non-empty dmaChannels
        auto dmaChannelsTree = mapping.second.get_child_optional("dmaChannels");
        tile.active_memory = (dmaChannelsTree && !dmaChannelsTree.get().empty());
        
        tiles.push_back(tile);
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

    for (auto const &mapping : kernelToTileMapping.get()) {
        std::string graphStr = mapping.second.get<std::string>("graph", "");
        if (graphStr.empty())
            continue; // Skip empty graph names
        
        if ((graphStr.find(graph_name) == std::string::npos) && (graph_name.compare("all") != 0))
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

        tile_type tile;
        tile.col = mapping.second.get<uint8_t>("column");
        tile.row = mapping.second.get<uint8_t>("row") + rowOffset;
        tile.active_core = isCoreUsed;
        tile.active_memory = isDMAUsed;
        
        tiles.push_back(tile);
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
// NOTE: This searches all DMA channels at the specified hardware coordinates,
// not just those associated with mapped tiles at those coordinates
std::vector<dma_channel_type>
AIETraceConfigV3Filetype::getDMAChannels(uint8_t column, uint8_t row) const
{
    std::vector<dma_channel_type> dmaChannels;
    auto allTileInfos = parseTileMappings();
    
    // Search all DMA channels in all mapped tiles for ones at the specified hardware coordinates
    for (const auto& tileInfo : allTileInfos) {
        for (const auto& dmaChannel : tileInfo.dmaChannels) {
            if (dmaChannel.column == column && dmaChannel.row == row) {
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
    
    // Handle specific use cases:
    // 1. If pattern is just a core specification like "core[1]", match any function with that core
    // 2. If pattern is an API name like "bf8x8_mid_api", match any function with that API
    // 3. If pattern is a full specification like "bf8x8_mid_api.core[1]", match exactly
    
    // First check for exact match of full function
    if (function == pattern) {
        return true;
    }
    
    // Check if pattern matches any part of the function
    if (function.find(pattern) != std::string::npos) {
        return true;
    }
    
    // Split function into parts and check each part
    std::vector<std::string> functionParts;
    boost::split(functionParts, function, boost::is_any_of("."));
    
    std::vector<std::string> patternParts;
    boost::split(patternParts, pattern, boost::is_any_of("."));
    
    // Check if pattern parts match function parts
    for (const auto& patternPart : patternParts) {
        bool foundMatch = false;
        for (const auto& functionPart : functionParts) {
            if (functionPart == patternPart || functionPart.find(patternPart) != std::string::npos) {
                foundMatch = true;
                break;
            }
        }
        if (!foundMatch) {
            return false; // All pattern parts must match
        }
    }
    
    return true;
}

// Get all unique tile coordinates (both core tiles and DMA-only tiles)
std::vector<std::pair<uint8_t, uint8_t>>
AIETraceConfigV3Filetype::getAllTileCoordinates() const
{
    std::set<std::pair<uint8_t, uint8_t>> uniqueCoordinates;
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        // Add core tile coordinates
        uniqueCoordinates.insert({tileInfo.column, tileInfo.row});
        
        // Add DMA channel coordinates (may be different from core tile)
        for (const auto& dmaChannel : tileInfo.dmaChannels) {
            uniqueCoordinates.insert({dmaChannel.column, dmaChannel.row});
        }
    }
    
    return std::vector<std::pair<uint8_t, uint8_t>>(uniqueCoordinates.begin(), uniqueCoordinates.end());
}

// Get coordinates of tiles that only have DMA channels (no cores)
std::vector<std::pair<uint8_t, uint8_t>>
AIETraceConfigV3Filetype::getDMAOnlyTileCoordinates() const
{
    std::set<std::pair<uint8_t, uint8_t>> mappedTileCoords;
    std::set<std::pair<uint8_t, uint8_t>> dmaCoords;
    auto allTileInfos = parseTileMappings();
    
    // Collect mapped tile coordinates and DMA coordinates
    for (const auto& tileInfo : allTileInfos) {
        mappedTileCoords.insert({tileInfo.column, tileInfo.row});
        
        for (const auto& dmaChannel : tileInfo.dmaChannels) {
            dmaCoords.insert({dmaChannel.column, dmaChannel.row});
        }
    }
    
    // Find DMA coordinates that are not mapped tile coordinates
    std::vector<std::pair<uint8_t, uint8_t>> dmaOnlyCoords;
    std::set_difference(dmaCoords.begin(), dmaCoords.end(),
                       mappedTileCoords.begin(), mappedTileCoords.end(),
                       std::back_inserter(dmaOnlyCoords));
    
    return dmaOnlyCoords;
}

// Get all DMA channels across all tiles
std::vector<dma_channel_type>
AIETraceConfigV3Filetype::getAllDMAChannels() const
{
    std::vector<dma_channel_type> allDmaChannels;
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        for (const auto& dmaChannel : tileInfo.dmaChannels) {
            allDmaChannels.push_back(dmaChannel);
        }
    }
    
    return allDmaChannels;
}

// Check if DMA channels exist at specific coordinates
bool
AIETraceConfigV3Filetype::hasDMAChannelsAt(uint8_t column, uint8_t row) const
{
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        for (const auto& dmaChannel : tileInfo.dmaChannels) {
            if (dmaChannel.column == column && dmaChannel.row == row) {
                return true;
            }
        }
    }
    
    return false;
}

// Get core tiles that have DMA channels at specific coordinates
std::vector<aie_tile_info>
AIETraceConfigV3Filetype::getCoreTilesWithDMAAt(uint8_t column, uint8_t row) const
{
    std::vector<aie_tile_info> coreTiles;
    auto allTileInfos = parseTileMappings();
    
    for (const auto& tileInfo : allTileInfos) {
        bool hasChannelAtCoords = false;
        
        for (const auto& dmaChannel : tileInfo.dmaChannels) {
            if (dmaChannel.column == column && dmaChannel.row == row) {
                hasChannelAtCoords = true;
                break;
            }
        }
        
        if (hasChannelAtCoords) {
            coreTiles.push_back(tileInfo);
        }
    }
    
    return coreTiles;
}

} // namespace xdp::aie
