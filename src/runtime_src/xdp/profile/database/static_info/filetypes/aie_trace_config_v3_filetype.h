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
};

} // namespace xdp::aie

#endif
