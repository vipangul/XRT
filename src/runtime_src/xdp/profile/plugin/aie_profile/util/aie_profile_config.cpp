/**
 * Copyright (C) 2022-2023 Advanced Micro Devices, Inc. - All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the "License"). You may
 * not use this file except in compliance with the License. A copy of the
 * License is located at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 * License for the specific language governing permissions and limitations
 * under the License.
 */

#define XDP_PLUGIN_SOURCE

#include "xdp/profile/plugin/aie_profile/util/aie_profile_config.h"
#include "xdp/profile/plugin/aie_profile/util/aie_profile_util.h"
#include "xdp/profile/database/static_info/aie_constructs.h"
#include "xdp/profile/database/static_info/aie_util.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include "core/common/message.h"


namespace xdp::aie::profile {
  using severity_level = xrt_core::message::severity_level;
  // using module_type = xdp::module_type;

  /****************************************************************************
   * Configure the individual AIE events for metric sets that use group events
   ***************************************************************************/
  void configGroupEvents(XAie_DevInst* aieDevInst, const XAie_LocType loc,
                          const XAie_ModuleType mod, const module_type type,
                          const std::string metricSet, const XAie_Events event,
                          const uint8_t channel) 
  {
    // Set masks for group events
    // NOTE: Group error enable register is blocked, so ignoring
    if (event == XAIE_EVENT_GROUP_DMA_ACTIVITY_MEM)
      XAie_EventGroupControl(aieDevInst, loc, mod, event, GROUP_DMA_MASK);
    else if (event == XAIE_EVENT_GROUP_LOCK_MEM)
      XAie_EventGroupControl(aieDevInst, loc, mod, event, GROUP_LOCK_MASK);
    else if (event == XAIE_EVENT_GROUP_MEMORY_CONFLICT_MEM)
      XAie_EventGroupControl(aieDevInst, loc, mod, event, GROUP_CONFLICT_MASK);
    else if (event == XAIE_EVENT_GROUP_CORE_PROGRAM_FLOW_CORE)
      XAie_EventGroupControl(aieDevInst, loc, mod, event, GROUP_CORE_PROGRAM_FLOW_MASK);
    else if (event == XAIE_EVENT_GROUP_CORE_STALL_CORE)
      XAie_EventGroupControl(aieDevInst, loc, mod, event, GROUP_CORE_STALL_MASK);
    else if (event == XAIE_EVENT_GROUP_DMA_ACTIVITY_PL) {
      uint32_t bitMask = aie::isInputSet(type, metricSet) 
          ? ((channel == 0) ? GROUP_SHIM_S2MM0_STALL_MASK : GROUP_SHIM_S2MM1_STALL_MASK)
          : ((channel == 0) ? GROUP_SHIM_MM2S0_STALL_MASK : GROUP_SHIM_MM2S1_STALL_MASK);
      XAie_EventGroupControl(aieDevInst, loc, mod, event, bitMask);
    }                                    
  }

  /****************************************************************************
   * Configure the selection index to monitor channel number in memory tiles
   ***************************************************************************/
  void configEventSelections(XAie_DevInst* aieDevInst,
                        const XAie_LocType loc,
                        const module_type type,
                        const std::string metricSet,
                        const uint8_t channel)
  {
    if (type != module_type::mem_tile)
      return;

    XAie_DmaDirection dmaDir = aie::isInputSet(type, metricSet) ? DMA_S2MM : DMA_MM2S;
    XAie_EventSelectDmaChannel(aieDevInst, loc, 0, dmaDir, channel);

    std::stringstream msg;
    msg << "Configured mem tile " << (aie::isInputSet(type,metricSet) ? "S2MM" : "MM2S") 
    << "DMA  for metricset " << metricSet << ", channel " << (int)channel << ".";
    xrt_core::message::send(severity_level::debug, "XRT", msg.str());
  } 

  /****************************************************************************
   * Configure AIE Core module start on graph iteration count threshold
   ***************************************************************************/
  bool configStartIteration(xaiefal::XAieMod& core, uint32_t iteration,
                            XAie_Events& retCounterEvent)
  {
    XAie_ModuleType mod = XAIE_CORE_MOD;
    // Count up by 1 for every iteration
    auto pc = core.perfCounter();
    if (pc->initialize(mod, XAIE_EVENT_INSTR_EVENT_0_CORE, 
                       mod, XAIE_EVENT_INSTR_EVENT_0_CORE) != XAIE_OK)
      return false;
    if (pc->reserve() != XAIE_OK)
      return false;

    xrt_core::message::send(severity_level::debug, "XRT", 
        "Configuring AIE trace to start on iteration " + std::to_string(iteration));

    pc->changeThreshold(iteration);
    
    XAie_Events counterEvent;
    pc->getCounterEvent(mod, counterEvent);
    // Reset when done counting
    pc->changeRstEvent(mod, counterEvent);
    if (pc->start() != XAIE_OK)
      return false;

    // Respond back with this performance counter event 
    // to use it later for broadcasting
    retCounterEvent = counterEvent;
    return true;
  }

  /****************************************************************************
   * Configure the broadcasting of provided module and event
   * (Brodcasted from AIE Tile core module)
   ***************************************************************************/
  void configEventBroadcast(XAie_DevInst* aieDevInst,
                        const XAie_LocType loc,
                        const module_type xdpModType,
                        const std::string metricSet,
                        const XAie_ModuleType xaieModType,
                        const XAie_Events bcEvent,
                        XAie_Events& bcChannelEvent)
  {
    if ((bcEvent != XAIE_EVENT_INSTR_EVENT_0_CORE) || (xaieModType != XAIE_CORE_MOD)
        || (xdpModType != module_type::core)) {
      std::cout << "!!! Warning: Not supported brodcast event or module type received. \n";
      return;
    }

    // Each module has 16 broadcast channels (0-15). It is safe to use 
    // later channel Ids considering other channel IDs being used.
    // Use by default brodcastId 10 for start_to_bytes_transferred & 
    // brodcastId 11 for interface_tile_latency
    // TODO: Use driver to dynamically get the brodcast channels
    uint8_t brodcastId = 10;  // Use API to get it runtime.
    if (metricSet == "interface_tile_latency")
      brodcastId = 11;  // Use API to get it runtime.

    int driverStatus   = AieRC::XAIE_OK;

    driverStatus |= XAie_EventBroadcast(aieDevInst, loc, xaieModType, brodcastId, bcEvent);
    if (driverStatus!= XAIE_OK) {
      std::cout << "!!! Configuration to broadcast event: " << bcEvent << " to module type: "
                << xaieModType << "has returned an error: " << driverStatus << std::endl;
    }

    bcChannelEvent = XAIE_EVENT_BROADCAST_10_CORE;
    if (metricSet == "interface_tile_latency")
      bcChannelEvent = XAIE_EVENT_BROADCAST_11_CORE;

  }

 
  /****************************************************************************
   * Configure the individual AIE events for metric sets related to Profile APIs
   ***************************************************************************/
   void configGraphIteratorAndBroadcast(XAie_DevInst* aieDevInst, xaiefal::XAieMod& core,
                      const XAie_LocType loc, const XAie_ModuleType xaieModType,
                      const module_type xdpModType, const std::string metricSet,
                      uint32_t iterCount, XAie_Events& bcEvent)
  {
    if (!aie::profile::isProfileAPIMetricSet(metricSet))
      return;
   
    if (metricSet == "start_to_bytes_transferred") {
      if (xdpModType == module_type::core) {
        XAie_Events counterEvent;
        // Step 1: Configure the graph iterator event
        if (!aie::profile::configStartIteration(core, iterCount, counterEvent))
          std::cout << "!!! Warning: couldn't retrieve graph iteration count.\n";
        
        // Step 2: Configure the brodcast of the returned counter event
        XAie_Events bcChannelEvent;
        configEventBroadcast(aieDevInst, loc, xdpModType, metricSet, xaieModType,
                             counterEvent, bcChannelEvent);
        // Store the brodcasted channel event for later use
        bcEvent = bcChannelEvent;
        } // core module
        else {
          // No usecase for memory tiles & shim tiles in 2024.2 release
          return;
        }
     }
     else if (metricSet == "interface_tile_latency") {
      std::cout << "!!! TODO: interface_tile_latency support." << std::endl;
     }
     else {
      std::cout << "!!! It shouldn't reach here." << std::endl;
     }
  }

  std::shared_ptr<xaiefal::XAiePerfCounter>
  configProfileAPICounters(XAie_DevInst* aieDevInst, xaiefal::XAieMod& xaieModule,
                           XAie_ModuleType& xaieModType, const module_type xdpModType,
                           const std::string& metricSet, XAie_Events startEvent,
                           XAie_Events endEvent, XAie_Events resetEvent,
                           int pcIndex, size_t threshold, XAie_Events& retCounterEvent)
  {
    if (xdpModType != module_type::shim)
      return nullptr;

    // Request counter from resource manager
    auto pc = xaieModule.perfCounter();
    auto ret = pc->initialize(xaieModType, startEvent, 
                                       xaieModType, endEvent,
                                       XAIE_PL_MOD, resetEvent);
    if (ret != XAIE_OK) return nullptr;
    ret = pc->reserve();
    if (ret != XAIE_OK) return nullptr;

    if (threshold > 0)
      pc->changeThreshold(threshold);

    XAie_Events counterEvent;
    pc->getCounterEvent(xaieModType, counterEvent);

    // Start the counter
    ret = pc->start();
    if (ret != XAIE_OK) return nullptr;
    
    // Respond back with this performance counter event 
    // to use it later for broadcasting
    retCounterEvent = counterEvent;
    return pc;
  }

  /****************************************************************************
   * Check if metric set is from Prof APIs Support
   ***************************************************************************/
  bool isProfileAPIMetricSet(const std::string metricSet)
  {
    // input_throughputs/output_throughputs is already supported, hence excluded here
    std::set<std::string> profAPIMetricSet = {"start_to_bytes_transferred", "interface_tile_latency"};
    return profAPIMetricSet.find(metricSet) != profAPIMetricSet.end();
  }

  /****************************************************************************
   * Configure performance counters in interface tile 
   * to profile running_event count or stalled_event count
   ***************************************************************************/
  // void configInterfaceTilesRunningOrStalledCount(xaiefal::XAieMod& mod, 
  //                                                XAie_ModuleType& startMod, 
  //                                                XAie_Events& startEvent,
  //                                                XAie_ModuleType& stopMod,
  //                                                XAie_Events& stopEvent,
  //                                                XAie_ModuleType& resetMod, 
  //                                                XAie_Events& resetEvent,
  //                                                uint32_t iteration)
  // {
  //   XAie_ModuleType mod = XAIE_PL_MOD;
    
  //   auto pc = mod.perfCounter();
  //   if (pc->initialize(startMod, startEvent, 
  //                      stopMod,  stopEvent,
  //                      resetMod, resetEvent) != XAIE_OK)
  //     return false;
  //   if (pc->reserve() != XAIE_OK)
  //     return false;

  //   xrt_core::message::send(severity_level::debug, "XRT", 
  //       "Configuring AIE profile to start on iteration " + std::to_string(iteration));

  //   pc->changeThreshold(iteration);
    
  //   XAie_Events counterEvent;
  //   pc->getCounterEvent(mod, counterEvent);
  //   // Reset when done counting
  //   pc->changeRstEvent(mod, counterEvent);
  //   if (pc->start() != XAIE_OK)
  //     return false;

  //   // Respond back with this performance counter event 
  //   // to use it later if needed.
  //   startEvent = counterEvent;
  //   return true;
  // }

  // /****************************************************************************
  //  * Configure performance counters in interface tile 
  //  * to profile StartToBytesTransferred
  //  ***************************************************************************/
  // void configInterfaceTilesPFCStartToBytesTransferred(xaiefal::XAieMod& startMod, 
  //                                                     XAie_ModuleType& startMod, 
  //                                                     XAie_Events& startEvent,
  //                                                     XAie_ModuleType& stopMod,
  //                                                     XAie_Events& stopEvent,
  //                                                     XAie_ModuleType& resetMod, 
  //                                                     XAie_Events& resetEvent,
  //                                                     uint32_t iteration)
  // {
  //   // TODO : In progress..
  // }

  // /****************************************************************************
  //  * Configure performance counters in destination interface tile 
  //  * to profile latency
  //  ***************************************************************************/
  // void configInterfaceTilesPFCLatencyDest(xaiefal::XAieMod& startMod, 
  //                                     XAie_ModuleType& startMod, 
  //                                     XAie_Events& startEvent,
  //                                     XAie_ModuleType& stopMod,
  //                                     XAie_Events& stopEvent,
  //                                     XAie_ModuleType& resetMod, 
  //                                     XAie_Events& resetEvent,
  //                                     uint32_t iteration)
  // {
  //   // We can re-use the existing API configInterfaceTilesRunningOrStalledCount()
  //   // Only start, stop & reset events will change.
  // }

  // /****************************************************************************
  //  * Configure performance counters in source interface tile 
  //  * to profile latency
  //  ***************************************************************************/
  // void configInterfaceTilesPFCLatencySrc(xaiefal::XAieMod& startMod, 
  //                                     XAie_ModuleType& startMod, 
  //                                     XAie_Events& startEvent,
  //                                     XAie_ModuleType& stopMod,
  //                                     XAie_Events& stopEvent,
  //                                     XAie_ModuleType& resetMod, 
  //                                     XAie_Events& resetEvent,
  //                                     uint32_t iteration)
  // {
  //   // We can re-use the existing API configInterfaceTilesRunningOrStalledCount()
  //   // Only start, stop & reset events will change.
  // }

  /****************************************************************************
   * Triger Latency profiling by configuring writing event_register 
   * in source interface tile to profile latency
   * 
   * This should be called only after all other necessary
   * configuration is completed
   ***************************************************************************/
  // void configInterfaceTilesLatencyTrigger(XAie_DevInst* aieDevInst,
  //                                                      const XAie_LocType tileloc,
  //                                                      const module_type modType,
  //                                                      const std::string metricSet,
  //                                                      const XAie_Events& userEventTrigger
  //                                                     )
  // {
  //   int driverStatus   = AieRC::XAIE_OK;
    
  //   uint8_t broadcastId = 11;
  //   driverStatus |= XAie_EventBroadcast(aieDevInst, tileloc, XAIE_PL_MOD, broadcastId, XAIE_EVENT_USER_EVENT_0_PL);
  //   if (driverStatus != XAIE_OK) {
  //     std::cout << "!!! Configuration of eventBroadcast for module type: "
  //               << modType << "has returned an error: " << driverStatus << std::endl;
  //   }

  //   driverStatus |= XAie_EventGenerate(aieDevInst, tileloc, modType, userEventTrigger);
  //   if (driverStatus != XAIE_OK) {
  //     std::cout << "!!! Configuration to userEventRegister: for module type: "
  //               << type << "has returned an error: " << driverStatus << std::endl;
  //   }
  // }


 
  /****************************************************************************
   * Configure combo events (Interface Tiles only)
   ***************************************************************************/
  // std::vector<XAie_Events>
  // configComboEvents(XAie_DevInst* aieDevInst, XAieTile& xaieTile, 
  //                   const XAie_LocType loc, const XAie_ModuleType mod,
  //                   const module_type type, const std::string metricSet,
  //                   aie_cfg_base& config)
  // {
  //   // Only needed for core/memory modules and metric sets that include DMA events
  //   if (!aie::profile::isProfileAPIMetricSet(metricSet) || (type != module_type::shim))
  //     return {};

  //   std::vector<XAie_Events> comboEvents;

  //   if (mod == XAIE_CORE_PL) {
  //     //auto comboEvent = xaieTile.core().comboEvent(4);
  //     comboEvents.push_back(XAIE_EVENT_COMBO_EVENT_2_PL);

  //     //TODO: This needs to come as an argument for 
  //     std::vector<XAie_Events> events = {XAIE_EVENT_PORT_RUNNING_0_PL,
  //                                        XAIE_EVENT_PERF_CNT_0_PL};
  //     std::vector<XAie_EventComboOps> opts = {XAIE_EVENT_COMBO_E1_AND_E2};

  //     // Set events and trigger on OR of events
  //     comboEvent->setEvents(events, opts);
  //   }
  //   return comboEvents;
  // }

}  // namespace xdp
