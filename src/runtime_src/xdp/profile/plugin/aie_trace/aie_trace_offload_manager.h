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
  bool offloadEnabledPLIO = false;
  bool offloadEnabledGMIO = false;

  void initPLIO(uint64_t deviceID, void* handle, PLDeviceIntf* deviceIntf, uint64_t bufSize, uint64_t numStreams, XAie_DevInst* devInst) {
    offloadEnabledPLIO = xrt_core::config::get_aie_trace_offload_plio_enabled();
    if (!offloadEnabledPLIO) {
      std::cout << "!!! AIETraceOffloadManager::initPLIO: PLIO offload disabled by config." << std::endl;
      return;
    }

    plio.logger = std::make_unique<AIETraceDataLogger>(deviceID, io_type::PLIO);
    plio.offloader = std::make_unique<AIETraceOffload>(handle, deviceID, deviceIntf, plio.logger.get(), true, bufSize, numStreams, devInst);
    plio.valid = true;
    std::cout << "!!! AIETraceOffloadManager::initPLIO called. numStreams: " << numStreams << std::endl;
  }

  #ifdef XDP_CLIENT_BUILD
void AIETraceOffloadManager::initGMIO(
    uint64_t deviceID, void* handle, PLDeviceIntf* deviceIntf,
    uint64_t bufSize, uint64_t numStreams, xrt::hw_context context, std::shared_ptr<AieTraceMetadata> metadata)
  {
    offloadEnabledGMIO = xrt_core::config::get_aie_trace_offload_gmio_enabled();
    if (!offloadEnabledGMIO) {
      std::cout << "!!! AIETraceOffloadManager::initPLIO: GMIO offload disabled by config." << std::endl;
      return;
    }

    gmio.logger = std::make_unique<AIETraceDataLogger>(deviceID, io_type::GMIO);
    // Use the client-specific AIETraceOffload constructor
    gmio.offloader = std::make_unique<AIETraceOffload>(
        handle, deviceID, deviceIntf, gmio.logger.get(), false, // isPLIO = false
        bufSize, numStreams, context, metadata);
    gmio.valid = true;
  }
#else
  void initGMIO(uint64_t deviceID, void* handle, PLDeviceIntf* deviceIntf, uint64_t bufSize, uint64_t numStreams, XAie_DevInst* devInst) {
    offloadEnabledGMIO = xrt_core::config::get_aie_trace_offload_gmio_enabled();
    if (!offloadEnabledGMIO) {
      std::cout << "!!! AIETraceOffloadManager::initPLIO: GMIO offload disabled by config." << std::endl;
      return;
    }

    gmio.logger = std::make_unique<AIETraceDataLogger>(deviceID, io_type::GMIO);
    gmio.offloader = std::make_unique<AIETraceOffload>(handle, deviceID, deviceIntf, gmio.logger.get(), false, bufSize, numStreams, devInst);
    gmio.valid = true;
    std::cout << "!!! AIETraceOffloadManager::initGMIO called. numStreams: " << numStreams << std::endl;
  }
#endif

  void startOffload(bool continuousTrace, uint64_t offloadIntervalUs){
    if (!offloadEnabledPLIO && !offloadEnabledGMIO) {
      std::cout << "!!! AIETraceOffloadManager::startOffload: Both PLIO and GMIO offload disabled by config." << std::endl;
      return;
    }
    if (offloadEnabledPLIO)
      startPLIOOffload(continuousTrace, offloadIntervalUs);
    if (offloadEnabledGMIO)
      startGMIOOffload(continuousTrace, offloadIntervalUs);
  }

  void startPLIOOffload(bool continuousTrace, uint64_t offloadIntervalUs) {
    std::cout << "!!! AIETraceOffloadManager::startPLIOOffload called." << std::endl;
    if (plio.offloader && continuousTrace) {
      plio.offloader->setContinuousTrace();
      plio.offloader->setOffloadIntervalUs(offloadIntervalUs);
    }
    if (plio.offloader)
      plio.offloader->startOffload();
  }

  void startGMIOOffload(bool continuousTrace, uint64_t offloadIntervalUs) {
    std::cout << "!!! AIETraceOffloadManager::startGMIOOffload called." << std::endl;
    if (gmio.offloader && continuousTrace) {
      gmio.offloader->setContinuousTrace(); // GMIO trace offload does not support continuous trace
      gmio.offloader->setOffloadIntervalUs(offloadIntervalUs);
    }
    if (gmio.offloader)
      gmio.offloader->startOffload();
  }

  bool initReadTraces() {
    bool ok = true;

    if (offloadEnabledPLIO && plio.offloader) {
      std::cout << "!!! AIETraceOffloadManager::initReadTraces: Initializing PLIO trace read." << std::endl;
      ok &= plio.offloader->initReadTrace();
    }
    if (offloadEnabledGMIO && gmio.offloader) {
      std::cout << "!!! AIETraceOffloadManager::initReadTraces: Initializing GMIO trace read." << std::endl;
      ok &= gmio.offloader->initReadTrace();
    }
    return ok;
  }

  void flushAll(bool warn) {
    if (offloadEnabledPLIO && plio.offloader) {
      std::cout << "!!! AIETraceOffloadManager::flushAll: Flushing PLIO traces." << std::endl;
      flushOffloader(plio.offloader, warn);
    }
    if (offloadEnabledGMIO && gmio.offloader) {
      std::cout << "!!! AIETraceOffloadManager::flushAll: Flushing GMIO traces." << std::endl;
      flushOffloader(gmio.offloader, warn);
    }
  }

  static void flushOffloader(const std::unique_ptr<AIETraceOffload>& offloader, bool warn) {
    std::cout << "!!! AIETraceOffloadManager::flushOffloader called." << std::endl;
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