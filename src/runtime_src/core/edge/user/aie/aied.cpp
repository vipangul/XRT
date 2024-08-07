/**
 * Copyright (C) 2020 Xilinx, Inc
 * Author(s): Himanshu Choudhary <hchoudha@xilinx.com>
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

#include <cstdio>
#include <cerrno>
#include <unistd.h>
#include <sstream>
#include <iostream>
#include <csignal>
#include "aied.h"
#include "core/edge/include/zynq_ioctl.h"
#include "core/edge/user/shim.h"
#include "core/common/config_reader.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

namespace zynqaie {

aied::aied(xrt_core::device* device): m_device(device)
{
  done = false;
  pthread_create(&ptid, NULL, &aied::poll_aie, this);
  pthread_setname_np(ptid, "Graph Status");
}

aied::~aied()
{
  done = true;
  pthread_kill(ptid, SIGUSR1);
  pthread_join(ptid, NULL);
}

/* Dummy signal handler for SIGTERM */
static void signal_handler(int signum) {}

void*
aied::poll_aie(void* arg)
{
  aied* ai = (aied*)arg;
  ZYNQ::shim *drv = ZYNQ::shim::handleCheck(ai->m_device->get_device_handle());
  xclAIECmd cmd;

  signal(SIGUSR1, signal_handler);

  if (!xrt_core::config::get_enable_aied())
    return NULL;

  /* Ever running thread */
  while (1) {
    /* Give up the cpu to the other threads. We are running this in an
     * infinite for loop */
    sleep(1);
    /* Calling XRT interface to wait for commands */
    if (ai->m_graphs.empty() || drv->xclAIEGetCmd(&cmd) != 0) {
      /* break if destructor called */
      if (ai->done)
        return NULL;
      continue;
    }

    switch (cmd.opcode) {
    case GRAPH_STATUS: {
      boost::property_tree::ptree pt;
      boost::property_tree::ptree pt_status;

      for (auto graph : ai->m_graphs) {
        pt.put(graph->getname(), graph->getstatus());
      }

      pt_status.add_child("graphs", pt);
      std::stringstream ss;
      boost::property_tree::json_parser::write_json(ss, pt_status);
      std::string tmp(ss.str());
      cmd.size = snprintf(cmd.info,(tmp.size() < AIE_INFO_SIZE) ? tmp.size():AIE_INFO_SIZE
                   , "%s\n", tmp.c_str());
      drv->xclAIEPutCmd(&cmd);
      break;
      }
    default:
      break;
    }

  }
}

void
aied::register_graph(const graph_object *graph)
{
  m_graphs.push_back(graph);
}

void
aied::deregister_graph(const graph_object *graph)
{
  m_graphs.erase(std::remove(m_graphs.begin(), m_graphs.end(), graph), m_graphs.end());
}
}
