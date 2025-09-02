// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved

#ifndef AIE_TRACE_CONFIG_V3_FILETYPE_DOT_H
#define AIE_TRACE_CONFIG_V3_FILETYPE_DOT_H

#include "aie_trace_config_filetype.h"
#include <boost/property_tree/ptree.hpp>

// ***************************************************************
// The implementation specific to the aie_trace_config.json file
// NOTE: built on top of aie_control_config.json implementation
// ***************************************************************
namespace xdp::aie {

class AIETraceConfigV3Filetype : public AIETraceConfigFiletype {
    public:
        AIETraceConfigV3Filetype(boost::property_tree::ptree& aie_project);
        ~AIETraceConfigV3Filetype() = default;

        std::vector<std::string>
        getValidKernels() const override;

        std::vector<std::string>
        getValidGraphs() const override;

        std::vector<tile_type>
        getTiles(const std::string& graph_name,
                 module_type type, 
                 const std::string& kernel_name = "all") const override;
        
        std::vector<tile_type>
        getAllAIETiles(const std::string& graph_name) const override;

        std::vector<tile_type> 
        getAIETiles(const std::string& graph_name) const override;
        
        std::vector<tile_type>
        getEventTiles(const std::string& graph_name, module_type type) const override;

        // New APIs for DMA channel support and enhanced querying
        std::vector<aie_tile_info>
        getAIETileInfos(const std::string& graph_name = "all",
                        const std::string& kernel_name = "all") const;

        std::vector<dma_channel_type>
        getDMAChannels(uint8_t column, uint8_t row) const;

        std::vector<dma_channel_type>
        getDMAChannelsByPortName(const std::string& portName) const;

        std::vector<aie_tile_info>
        getAIETilesByType(const std::string& tile_type = "aie") const;

        std::vector<aie_tile_info>
        filterTilesByGraphFunction(const std::string& graph_pattern,
                                   const std::string& function_pattern) const;

        // Enhanced APIs to handle DMA channels on different tiles
        std::vector<std::pair<uint8_t, uint8_t>>
        getAllTileCoordinates() const;

        std::vector<std::pair<uint8_t, uint8_t>>
        getDMAOnlyTileCoordinates() const;

        std::vector<dma_channel_type>
        getAllDMAChannels() const;

        bool
        hasDMAChannelsAt(uint8_t column, uint8_t row) const;

        std::vector<aie_tile_info>
        getCoreTilesWithDMAAt(uint8_t column, uint8_t row) const;

    private:
        // Helper methods
        bool matchesGraphPattern(const std::string& graph, const std::string& pattern) const;
        bool matchesFunctionPattern(const std::string& function, const std::string& pattern) const;
        bool matchesKernelPattern(const std::string& function, const std::string& kernel_name) const;
        std::vector<aie_tile_info> parseTileMappings() const;
};

} // namespace xdp::aie

#endif
