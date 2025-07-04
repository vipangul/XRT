// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2022-2025 Advanced Micro Devices, Inc. All rights reserved.

// This file implements XRT xclbin APIs as declared in
// core/include/experimental/xrt_queue.h
#define XRT_API_SOURCE         // exporting xrt_hwcontext.h
#define XCL_DRIVER_DLL_EXPORT  // exporting xrt_xclbin.h
#define XRT_CORE_COMMON_SOURCE // in same dll as coreutil

#include "core/include/xrt/xrt_hw_context.h"
#include "core/include/xrt/experimental/xrt_module.h"
#include "elf_int.h"
#include "hw_context_int.h"
#include "module_int.h"
#include "xclbin_int.h"

#include "core/common/device.h"
#include "core/common/trace.h"
#include "core/common/shim/hwctx_handle.h"
#include "core/common/usage_metrics.h"
#include "core/common/xdp/profile.h"

#include <limits>
#include <memory>

namespace xrt {

// class hw_context_impl - insulated implemention of an xrt::hw_context
//
class hw_context_impl : public std::enable_shared_from_this<hw_context_impl>
{
  using cfg_param_type = xrt::hw_context::cfg_param_type;
  using qos_type = cfg_param_type;
  using access_mode = xrt::hw_context::access_mode;

  std::shared_ptr<xrt_core::device> m_core_device;
  xrt::xclbin m_xclbin;
  std::map<std::string, xrt::module> m_module_map; // map b/w kernel name and module
  uint32_t m_partition_size = 0;
  cfg_param_type m_cfg_param;
  access_mode m_mode;
  std::unique_ptr<xrt_core::hwctx_handle> m_hdl;
  std::shared_ptr<xrt_core::usage_metrics::base_logger> m_usage_logger =
      xrt_core::usage_metrics::get_usage_metrics_logger();

  void
  create_module_map(const xrt::elf& elf)
  {
    xrt::module module_obj{elf};

    // Store the module in the map against all available kernels in the ELF
    // This will be useful for module lookup when creating xrt::kernel object
    // using kernel name
    const auto& kernels_info = xrt_core::module_int::get_kernels_info(module_obj);
    for (const auto& k_info : kernels_info) {
      auto kernel_name = k_info.props.name;
      if (m_module_map.find(kernel_name) != m_module_map.end())
        throw std::runtime_error("kernel already exists, cannot use this ELF with this hw ctx\n");

      m_module_map.emplace(std::move(kernel_name), module_obj);
    }
  }

public:
  hw_context_impl(std::shared_ptr<xrt_core::device> device, const xrt::uuid& xclbin_id, cfg_param_type cfg_param)
    : m_core_device(std::move(device))
    , m_xclbin(m_core_device->get_xclbin(xclbin_id))
    , m_cfg_param(std::move(cfg_param))
    , m_mode(xrt::hw_context::access_mode::shared)
    , m_hdl{m_core_device->create_hw_context(xclbin_id, m_cfg_param, m_mode)}
  {
  }

  hw_context_impl(std::shared_ptr<xrt_core::device> device, const xrt::uuid& xclbin_id, access_mode mode)
    : m_core_device{std::move(device)}
    , m_xclbin{m_core_device->get_xclbin(xclbin_id)}
    , m_mode{mode}
    , m_hdl{m_core_device->create_hw_context(xclbin_id, m_cfg_param, m_mode)}
  {}

  hw_context_impl(std::shared_ptr<xrt_core::device> device, cfg_param_type cfg_param, access_mode mode)
    : m_core_device{std::move(device)}
    , m_cfg_param{std::move(cfg_param)}
    , m_mode{mode}
  {}

  hw_context_impl(std::shared_ptr<xrt_core::device> device, const xrt::elf& elf,
                  cfg_param_type cfg_param, access_mode mode)
    : m_core_device{std::move(device)}
    , m_partition_size{xrt_core::elf_int::get_partition_size(elf)}
    , m_cfg_param{std::move(cfg_param)}
    , m_mode{mode}
    , m_hdl{m_core_device->create_hw_context(elf, m_cfg_param, m_mode)}
  {
    create_module_map(elf);
  }

  std::shared_ptr<hw_context_impl>
  get_shared_ptr()
  {
    return shared_from_this();
  }

  ~hw_context_impl()
  {
    // This trace point measures the time to tear down a hw context on the device
    XRT_TRACE_POINT_SCOPE(xrt_hw_context_dtor);

    try {
      // finish_flush_device should only be called when the underlying 
      // hw_context_impl is destroyed. The xdp::update_device cannot exist
      // in the hw_context_impl constructor because an existing
      // shared pointer must already exist to call get_shared_ptr(),
      // which is not true at that time.
      xrt_core::xdp::finish_flush_device(this);
      
      // Reset within scope of dtor for trace point to measure time to reset
      m_hdl.reset();
    }
    catch (...) {
      // ignore, dtor cannot throw
    }
  }

  hw_context_impl() = delete;
  hw_context_impl(const hw_context_impl&) = delete;
  hw_context_impl(hw_context_impl&&) = delete;
  hw_context_impl& operator=(const hw_context_impl&) = delete;
  hw_context_impl& operator=(hw_context_impl&&) = delete;

  void
  add_config(const xrt::elf& elf)
  {
    auto part_size = xrt_core::elf_int::get_partition_size(elf);

    // create hw ctx handle if not already created
    if (!m_hdl) {
      m_hdl = m_core_device->create_hw_context(elf, m_cfg_param, m_mode);
      m_partition_size = part_size;
      create_module_map(elf);
      return;
    }

    // add module only if partition size matches existing configuration
    if (m_partition_size != part_size)
      throw std::runtime_error("can not add config to ctx with different configuration\n");

    // Add kernels available in ELF to module map
    // This function throws if kernel with same name is already present
    create_module_map(elf);
  }

  void
  update_qos(const qos_type& qos)
  {
    m_hdl->update_qos(qos);
  }

  void
  set_exclusive()
  {
    m_mode = xrt::hw_context::access_mode::exclusive;
    m_hdl->update_access_mode(m_mode);
  }

  const std::shared_ptr<xrt_core::device>&
  get_core_device() const
  {
    return m_core_device;
  }

  xrt::uuid
  get_uuid() const
  {
    return m_xclbin.get_uuid();
  }

  xrt::xclbin
  get_xclbin() const
  {
    return m_xclbin;
  }

  access_mode
  get_mode() const
  {
    return m_mode;
  }

  size_t
  get_partition_size() const
  {
    return m_partition_size;
  }

  xrt_core::hwctx_handle*
  get_hwctx_handle()
  {
    return m_hdl.get();
  }

  xrt_core::usage_metrics::base_logger*
  get_usage_logger()
  {
    return m_usage_logger.get();
  }

  xrt::module
  get_module(const std::string& kname) const
  {
    if (auto itr = m_module_map.find(kname); itr != m_module_map.end())
      return itr->second;

    throw std::runtime_error("no module found with given kernel name in ctx");
  }
};

} // xrt

////////////////////////////////////////////////////////////////
// xrt_hw_context implementation of extension APIs not exposed to end-user
////////////////////////////////////////////////////////////////
namespace xrt_core::hw_context_int {

std::shared_ptr<xrt_core::device>
get_core_device(const xrt::hw_context& hwctx)
{
  return hwctx.get_handle()->get_core_device();
}

xrt_core::device*
get_core_device_raw(const xrt::hw_context& hwctx)
{
  return hwctx.get_handle()->get_core_device().get();
}

void
set_exclusive(xrt::hw_context& hwctx)
{
  hwctx.get_handle()->set_exclusive();
}

xrt::hw_context
create_hw_context_from_implementation(void* hwctx_impl)
{
  if (!hwctx_impl)
    throw std::runtime_error("Invalid hardware context implementation."); 

  auto impl_ptr = static_cast<xrt::hw_context_impl*>(hwctx_impl);
  return xrt::hw_context(impl_ptr->get_shared_ptr());
}

xrt::module
get_module(const xrt::hw_context& ctx, const std::string& kname)
{
  return ctx.get_handle()->get_module(kname);
}

size_t
get_partition_size(const xrt::hw_context& ctx)
{
  return ctx.get_handle()->get_partition_size();
}

} // xrt_core::hw_context_int

////////////////////////////////////////////////////////////////
// xrt_hwcontext C++ API implmentations (xrt_hw_context.h)
////////////////////////////////////////////////////////////////
namespace xrt {
// common function called with hw ctx created from different ways
static std::shared_ptr<hw_context_impl>
post_alloc_hwctx(const std::shared_ptr<hw_context_impl>& handle)
{
  // Update device is called with a raw pointer to dyanamically
  // link to callbacks that exist in XDP via a C-style interface
  // The create_hw_context_from_implementation function is then 
  // called in XDP create a hw_context to the underlying implementation
  xrt_core::xdp::update_device(handle.get(), true);
  handle->get_usage_logger()->log_hw_ctx_info(handle.get());
  return handle;
}

static std::shared_ptr<hw_context_impl>
alloc_hwctx_from_cfg(const xrt::device& device, const xrt::uuid& xclbin_id, const xrt::hw_context::cfg_param_type& cfg_param)
{
  XRT_TRACE_POINT_SCOPE(xrt_hw_context);
  return post_alloc_hwctx(std::make_shared<hw_context_impl>(device.get_handle(), xclbin_id, cfg_param));
}

static std::shared_ptr<hw_context_impl>
alloc_hwctx_from_mode(const xrt::device& device, const xrt::uuid& xclbin_id, xrt::hw_context::access_mode mode)
{
  XRT_TRACE_POINT_SCOPE(xrt_hw_context);
  return post_alloc_hwctx(std::make_shared<hw_context_impl>(device.get_handle(), xclbin_id, mode));
}

static std::shared_ptr<hw_context_impl>
alloc_empty_hwctx(const xrt::device& device, const xrt::hw_context::cfg_param_type& cfg_param, xrt::hw_context::access_mode mode)
{
  XRT_TRACE_POINT_SCOPE(xrt_hw_context);
  return post_alloc_hwctx(std::make_shared<hw_context_impl>(device.get_handle(), cfg_param, mode));
}

static std::shared_ptr<hw_context_impl>
alloc_hwctx_from_elf(const xrt::device& device, const xrt::elf& elf, const xrt::hw_context::cfg_param_type& cfg_param,
                     xrt::hw_context::access_mode mode)
{
  XRT_TRACE_POINT_SCOPE(xrt_hw_context);
  return post_alloc_hwctx(std::make_shared<hw_context_impl>(device.get_handle(), elf, cfg_param, mode));
}

hw_context::
hw_context(const xrt::device& device, const xrt::uuid& xclbin_id, const xrt::hw_context::cfg_param_type& cfg_param)
  : detail::pimpl<hw_context_impl>(alloc_hwctx_from_cfg(device, xclbin_id, cfg_param))
{}

hw_context::
hw_context(const xrt::device& device, const xrt::uuid& xclbin_id, access_mode mode)
  : detail::pimpl<hw_context_impl>(alloc_hwctx_from_mode(device, xclbin_id, mode))
{}

hw_context::
hw_context(const xrt::device& device, const xrt::elf& elf, const cfg_param_type& cfg_param, access_mode mode)
  : detail::pimpl<hw_context_impl>(alloc_hwctx_from_elf(device, elf, cfg_param, mode))
{}

hw_context::
hw_context(const xrt::device& device, const xrt::elf& elf)
  : hw_context(device, elf, {}, access_mode::shared)
{}

hw_context::
hw_context(const xrt::device& device, const cfg_param_type& cfg_param, access_mode mode)
  : detail::pimpl<hw_context_impl>(alloc_empty_hwctx(device, cfg_param, mode))
{}

void
hw_context::
add_config(const xrt::elf& elf)
{
  get_handle()->add_config(elf);
}

void
hw_context::
update_qos(const qos_type& qos)
{
  XRT_TRACE_POINT_SCOPE(xrt_hw_context_update_qos);
  get_handle()->update_qos(qos);
}

xrt::device
hw_context::
get_device() const
{
  return xrt::device{get_handle()->get_core_device()};
}

xrt::uuid
hw_context::
get_xclbin_uuid() const
{
  return get_handle()->get_uuid();
}

xrt::xclbin
hw_context::
get_xclbin() const
{
  return get_handle()->get_xclbin();
}

hw_context::access_mode
hw_context::
get_mode() const
{
  return get_handle()->get_mode();
}

hw_context::
operator xrt_core::hwctx_handle* () const
{
  return get_handle()->get_hwctx_handle();
}

hw_context::
~hw_context()
{}

} // xrt

////////////////////////////////////////////////////////////////
// xrt_aie_hw_context C++ API implmentations (xrt_aie.h)
////////////////////////////////////////////////////////////////
namespace xrt::aie {

void
hw_context::
reset_array()
{
  auto core_handle = get_handle()->get_hwctx_handle();
  core_handle->reset_array();
}
} //xrt::aie
