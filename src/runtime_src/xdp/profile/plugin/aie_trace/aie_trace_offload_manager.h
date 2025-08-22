#ifndef AIE_TRACE_OFFLOAD_MANAGER_H
#define AIE_TRACE_OFFLOAD_MANAGER_H

#include <iostream>
#include <sstream>

#include <functional>
#include <vector>
#include <string>
#include <memory>
#include "core/common/message.h"
#include "xdp/profile/database/events/creator/aie_trace_data_logger.h"
#include "xdp/profile/writer/aie_trace/aie_trace_writer.h"
#include "xdp/profile/plugin/aie_trace/aie_trace_impl.h"
#include "xdp/profile/device/pl_device_intf.h"
#include "xdp/profile/database/static_info/pl_constructs.h"
#include "xdp/profile/database/database.h"

// #include "xdp/profile/device/aie_trace/aie_trace_offload.h"
#ifdef XDP_CLIENT_BUILD
#include "xdp/profile/device/aie_trace/client/aie_trace_offload_client.h"
#elif XDP_VE2_BUILD
#include "xdp/profile/device/aie_trace/ve2/aie_trace_offload_ve2.h"
#else
#include "xdp/profile/device/aie_trace/aie_trace_offload.h"
#endif

extern "C" {
#include <xaiengine.h>
#include <xaiengine/xaiegbl_params.h>
}


namespace xdp {
  using severity_level = xrt_core::message::severity_level;

  struct AIETraceOffloadData {
    bool valid = false;
    std::unique_ptr<AIETraceLogger> logger;
    std::unique_ptr<AIETraceOffload> offloader;
  };

class AIETraceOffloadManager {
  private:
  // void printFlagValues(const char* caller = __func__) {
  //   std::cout << "!!! " << caller << " - offloadEnabledPLIO: " << offloadEnabledPLIO
  //             << ", offloadEnabledGMIO: " << offloadEnabledGMIO << std::endl;
  // }

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

static uint64_t checkAndCapToBankSize(VPDatabase* db,
                                 uint64_t device_id,
                                 uint8_t memIndex,
                                 uint64_t desired)
{
  auto* memory = db->getStaticInfo().getMemory(device_id, memIndex);
  if (!memory)
    return desired;

  const uint64_t fullBankSize = static_cast<uint64_t>(memory->size) * 1024ULL;
  if ((fullBankSize > 0) && (desired > fullBankSize)) {
    xrt_core::message::send(severity_level::warning, "XRT",
      "Requested AIE trace buffer is too big for memory resource. Limiting to "
      + std::to_string(fullBankSize) + ".");
    return fullBankSize;
  }
  return desired;
}

  uint64_t deviceID;
  VPDatabase* db;
  AieTraceImpl* aieTraceImpl = nullptr;


  AIETraceOffloadData plio;
  AIETraceOffloadData gmio;
  bool offloadEnabledPLIO = false;
  bool offloadEnabledGMIO = false;

  public:
  AIETraceOffloadManager(uint64_t device_id, VPDatabase* database, AieTraceImpl* impl = nullptr)
    : deviceID{device_id},
      db{database},
      aieTraceImpl{impl},
      offloadEnabledPLIO(xrt_core::config::get_aie_trace_offload_plio_enabled()),
      offloadEnabledGMIO(xrt_core::config::get_aie_trace_offload_gmio_enabled())
  {}

  void initPLIO(uint64_t device_id, void* handle, PLDeviceIntf* deviceIntf, uint64_t bufSize, uint64_t numStreams, XAie_DevInst* devInst) {
    offloadEnabledPLIO = xrt_core::config::get_aie_trace_offload_plio_enabled();
    if (!offloadEnabledPLIO) {
      std::cout << "!!! AIETraceOffloadManager::initPLIO: PLIO offload disabled by config, skipping.." << std::endl;
      return;
    }

    plio.logger = std::make_unique<AIETraceDataLogger>(device_id, io_type::PLIO);
#ifndef XDP_CLIENT_BUILD
    plio.offloader = std::make_unique<AIETraceOffload>(handle, device_id, deviceIntf, plio.logger.get(), true, bufSize, numStreams, devInst);
#else
    // Suppress unused parameter warnings in client build
    (void)handle;
    (void)deviceIntf;
    (void)bufSize;
    (void)numStreams;
    (void)devInst;
#endif
    plio.valid = true;
    std::cout << "!!! AIETraceOffloadManager::initPLIO called. numStreams: " << numStreams << std::endl;
  }

  // TODO: Use const references for parameters where applicable
  #ifdef XDP_CLIENT_BUILD
  void AIETraceOffloadManager::initGMIO(
              uint64_t device_id, void* handle, PLDeviceIntf* deviceIntf,
              uint64_t bufSize, uint64_t numStreams, xrt::hw_context context, std::shared_ptr<AieTraceMetadata> metadata)
  {
    offloadEnabledGMIO = xrt_core::config::get_aie_trace_offload_gmio_enabled();
    if (!offloadEnabledGMIO) {
      std::cout << "!!! AIETraceOffloadManager::initGMIO: GMIO offload disabled by config, skipping..." << std::endl;
      return;
    }

    gmio.logger = std::make_unique<AIETraceDataLogger>(device_id, io_type::GMIO);
    // Use the client-specific AIETraceOffload constructor
    gmio.offloader = std::make_unique<AIETraceOffload>(
        handle, device_id, deviceIntf, gmio.logger.get(), false, // isPLIO = false
        bufSize, numStreams, context, metadata);
    gmio.valid = true;
  }
#else
  void initGMIO(uint64_t device_id, void* handle, PLDeviceIntf* deviceIntf, uint64_t bufSize, uint64_t numStreams, XAie_DevInst* devInst) {
    offloadEnabledGMIO = xrt_core::config::get_aie_trace_offload_gmio_enabled();
    if (!offloadEnabledGMIO) {
      std::cout << "!!! AIETraceOffloadManager::initGMIO: GMIO offload disabled by config, skipping" << std::endl;
      return;
    }

    gmio.logger = std::make_unique<AIETraceDataLogger>(device_id, io_type::GMIO);
    gmio.offloader = std::make_unique<AIETraceOffload>(handle, device_id, deviceIntf, gmio.logger.get(), false, bufSize, numStreams, devInst);
    gmio.valid = true;
    std::cout << "!!! AIETraceOffloadManager::initGMIO called. numStreams: " << numStreams << std::endl;
  }
#endif

  void startOffload(bool continuousTrace, uint64_t offloadIntervalUs){
    // printFlagValues(__func__);
    if (!offloadEnabledPLIO && !offloadEnabledGMIO) {
      std::cout << "!!! AIETraceOffloadManager::startOffload: Both PLIO and GMIO offload disabled by config." << std::endl;
      return;
    }
    if (offloadEnabledPLIO)
      startPLIOOffload(continuousTrace, offloadIntervalUs);
    if (offloadEnabledGMIO)
      startGMIOOffload(continuousTrace, offloadIntervalUs);
  }

  bool initReadTraces() {
    bool ok = true;
    // printFlagValues(__func__);

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
    // printFlagValues(__func__);

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



  void createTraceWriters(uint64_t device_id, uint64_t numStreamsPLIO, uint64_t numStreamsGMIO, std::vector<VPWriter*>& writers) {
    if (offloadEnabledPLIO) {
      // Add writer for every PLIO stream
      for (uint64_t n = 0; n < numStreamsPLIO; ++n) {
        std::string fileName = "aie_trace_plio_" + std::to_string(device_id) + "_" +
                              std::to_string(n) + ".txt";
        VPWriter *writer = new AIETraceWriter(
          fileName.c_str(),
          device_id,
          n,  // stream id
          "", // version
          "", // creation time
          "", // xrt version
          "",  // tool version
          io_type::PLIO // offload type
        );
        writers.push_back(writer);
        db->getStaticInfo().addOpenedFile(writer->getcurrentFileName(),
                                          "AIE_EVENT_TRACE");

        std::stringstream msg;
        msg << "Creating AIE trace file " << fileName << " for device " << device_id;
        xrt_core::message::send(severity_level::info, "XRT", msg.str());
      }
    }

    if (offloadEnabledGMIO) {
      // Add writer for every GMIO stream
      for (uint64_t n = 0; n < numStreamsGMIO; ++n) {
        std::string fileName = "aie_trace_gmio_" + std::to_string(device_id) + "_" +
                              std::to_string(n) + ".txt";
        VPWriter *writer = new AIETraceWriter(
          fileName.c_str(),
          device_id,
          n,  // stream id
          "", // version
          "", // creation time
          "", // xrt version
          "",  // tool version
          io_type::GMIO // offload type
        );
        writers.push_back(writer);
        db->getStaticInfo().addOpenedFile(writer->getcurrentFileName(),
                                          "AIE_EVENT_TRACE");

        std::stringstream msg;
        msg << "Creating AIE trace file " << fileName << " for device " << device_id;
        xrt_core::message::send(severity_level::info, "XRT", msg.str());
      }
    }
  }


bool configureAndInitPLIO(
  uint64_t device_id, void* handle, PLDeviceIntf* deviceIntf,
  uint64_t desiredBufSize, uint64_t numStreamsPLIO, XAie_DevInst* devInst)
{
  uint64_t sz = aieTraceImpl ? aieTraceImpl->checkTraceBufSize(desiredBufSize) : desiredBufSize;

  uint8_t memIndex = 0;
  if (deviceIntf)
    memIndex = deviceIntf->getAIETs2mmMemIndex(0);

  desiredBufSize = checkAndCapToBankSize(db, device_id, memIndex, desiredBufSize);
  desiredBufSize = aieTraceImpl->checkTraceBufSize(desiredBufSize);

  if (!devInst) {
    xrt_core::message::send(severity_level::warning, "XRT",
      "Unable to get AIE device instance. AIE event trace will not be available.");
    return false;
  }

  initPLIO(device_id, handle, deviceIntf, sz, numStreamsPLIO, devInst);
  return true;
}

bool configureAndInitGMIO(
  uint64_t device_id, void* handle, PLDeviceIntf* deviceIntf,
  uint64_t desiredBufSize, uint64_t numStreamsGMIO
#ifdef XDP_CLIENT_BUILD
  , const xrt::hw_context& hwctx, const std::shared_ptr<AieTraceMetadata>& md
#else
  , XAie_DevInst* devInst
#endif
  )
{
  desiredBufSize = checkAndCapToBankSize(db, device_id, /*bank 0*/ 0, desiredBufSize);
  desiredBufSize = aieTraceImpl->checkTraceBufSize(desiredBufSize);

#ifdef XDP_CLIENT_BUILD
  initGMIO(device_id, handle, deviceIntf, desiredBufSize, numStreamsGMIO, hwctx, md);
  return true;
#else
  if (!devInst) {
    xrt_core::message::send(severity_level::warning, "XRT",
      "Unable to get AIE device instance. AIE event trace will not be available.");
    return false;
  }
  initGMIO(device_id, handle, deviceIntf, desiredBufSize, numStreamsGMIO, devInst);
  return true;
#endif
}

}; // class AIETraceOffloadManager

} //namespace xdp

#endif // AIE_TRACE_OFFLOAD_MANAGER_H