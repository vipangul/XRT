#ifndef AIE_TRACE_OFFLOAD_MANAGER_H
#define AIE_TRACE_OFFLOAD_MANAGER_H

#include <memory>
#include "core/common/message.h"
#include "xdp/profile/database/events/creator/aie_trace_data_logger.h"
// #include "xdp/profile/device/aie_trace/aie_trace_offload.h"
#ifdef XDP_CLIENT_BUILD
#include "xdp/profile/device/aie_trace/client/aie_trace_offload_client.h"
#elif XDP_VE2_BUILD
#include "xdp/profile/device/aie_trace/ve2/aie_trace_offload_ve2.h"
#else
#include "xdp/profile/device/aie_trace/aie_trace_offload.h"
#endif



namespace xdp {
  struct AIETraceOffloadData {
    bool valid = false;
    std::unique_ptr<AIETraceLogger> logger;
    std::unique_ptr<AIETraceOffload> offloader;
  };

class AIETraceOffloadManager {
public:
  AIETraceOffloadData plio;
  AIETraceOffloadData gmio;

  void initPLIO(uint64_t deviceID, void* handle, PLDeviceIntf* deviceIntf, uint64_t bufSize, uint64_t numStreams, XAie_DevInst* devInst) {
    plio.logger = std::make_unique<AIETraceDataLogger>(deviceID, io_type::PLIO);
    plio.offloader = std::make_unique<AIETraceOffload>(handle, deviceID, deviceIntf, plio.logger.get(), true, bufSize, numStreams, devInst);
    plio.valid = true;
  }

  #ifdef XDP_CLIENT_BUILD
void AIETraceOffloadManager::initGMIO(
    uint64_t deviceID, void* handle, PLDeviceIntf* deviceIntf,
    uint64_t bufSize, uint64_t numStreams, xrt::hw_context context, std::shared_ptr<AieTraceMetadata> metadata)
  {
    gmio.logger = std::make_unique<AIETraceDataLogger>(deviceID, io_type::GMIO);
    // Use the client-specific AIETraceOffload constructor
    gmio.offloader = std::make_unique<AIETraceOffload>(
        handle, deviceID, deviceIntf, gmio.logger.get(), false, // isPLIO = false
        bufSize, numStreams, context, metadata);
    gmio.valid = true;
  }
#else
  void initGMIO(uint64_t deviceID, void* handle, PLDeviceIntf* deviceIntf, uint64_t bufSize, uint64_t numStreams, XAie_DevInst* devInst) {
    gmio.logger = std::make_unique<AIETraceDataLogger>(deviceID, io_type::GMIO);
    gmio.offloader = std::make_unique<AIETraceOffload>(handle, deviceID, deviceIntf, gmio.logger.get(), false, bufSize, numStreams, devInst);
    gmio.valid = true;
  }
#endif

  void startOffload(bool continuousTrace, uint64_t offloadIntervalUs){
    startPLIOOffload(continuousTrace, offloadIntervalUs);
    startGMIOOffload(continuousTrace, offloadIntervalUs);
    // if (plio.offloader)
    //   plio.offloader->startPLIOOffload();
    // if (gmio.offloader)
    //   gmio.offloader->startGMIOOffload();
  }

  void startPLIOOffload(bool continuousTrace, uint64_t offloadIntervalUs) {
    if (plio.offloader && continuousTrace) {
      plio.offloader->setContinuousTrace();
      plio.offloader->setOffloadIntervalUs(offloadIntervalUs);
    }
    if (plio.offloader)
      plio.offloader->startOffload();
  }

  void startGMIOOffload(bool continuousTrace, uint64_t offloadIntervalUs) {
    if (gmio.offloader && continuousTrace) {
      // gmio.offloader->setContinuousTrace(); // GMIO trace offload does not support continuous trace
      // gmio.offloader->setOffloadIntervalUs(offloadIntervalUs);
    }
    if (gmio.offloader)
      gmio.offloader->startOffload();
  }

  bool initReadTraces() {
    bool ok = true;
    if (plio.offloader) ok &= plio.offloader->initReadTrace();
    if (gmio.offloader) ok &= gmio.offloader->initReadTrace();
    return ok;
  }

  void flushAll(bool warn) {
    if (plio.offloader) flushOffloader(plio.offloader, warn);
    if (gmio.offloader) flushOffloader(gmio.offloader, warn);
  }

  static void flushOffloader(const std::unique_ptr<AIETraceOffload>& offloader, bool warn) {
    if (offloader->continuousTrace()) {
      offloader->stopOffload();
      while (offloader->getOffloadStatus() != AIEOffloadThreadStatus::STOPPED) {}
    } else {
      offloader->readTrace(true);
      offloader->endReadTrace();
    }
    if (warn && offloader->isTraceBufferFull()) {
      xrt_core::message::send(xrt_core::message::severity_level::warning, "XRT", AIE_TS2MM_WARN_MSG_BUF_FULL);
    }
  }
}; // class AIETraceOffloadManager

} //namespace xdp

#endif // AIE_TRACE_OFFLOAD_MANAGER_H