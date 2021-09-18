#pragma once
#include <cassert>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <vulkan\vulkan.hpp>
#include "vkutils.hpp"

namespace vw {
constexpr std::array<const char*, 1> DebugValidationLayers = {"VK_LAYER_KHRONOS_validation"};

class Extent {
 public:
  Extent(uint32_t width, uint32_t height, uint32_t depth = 1) : width{width}, height{height}, depth{depth} {}
  inline operator vk::Extent2D() const {
    return {width, height};
  };
  inline operator vk::Extent3D() const {
    return {width, height, depth};
  };
  uint32_t width, height, depth;
};
class Semaphore : public vw::HandleContainerUnique<vk::Semaphore> {
 public:
  Semaphore(vk::Device device);
};

class Fence : public vw::HandleContainerUnique<vk::Fence> {
 public:
  Fence(vk::Device device) : ContainerType{device} {
    mHandle = mDeviceHandle.createFence({});
  }
  bool signaled() const {
    return mDeviceHandle.getFenceStatus(mHandle) == vk::Result::eSuccess;
  }
  void reset() {
    mDeviceHandle.resetFences(mHandle);
  }
  void wait() {
    mDeviceHandle.waitForFences(mHandle, true, 1000000);
  }
};

class CommandBuffer : public vk::CommandBuffer {
 public:
  enum class State { Initial, Recording, Executable, Pending, Invalid };
  CommandBuffer(vk::CommandBuffer commandBuffer) : vk::CommandBuffer{commandBuffer} {}
  template <typename T>
  inline void record(vk::CommandBufferUsageFlags flags, T recordFunc) {
    vk::CommandBuffer::begin(vk::CommandBufferBeginInfo{flags});
    mState = State::Recording;
    recordFunc(*this);
    vk::CommandBuffer::end();
    mState = State::Executable;
  }
  inline void beginRenderPass(vk::RenderPass renderPass,
                              vk::Framebuffer framebuffer,
                              const vk::Rect2D& renderArea,
                              ArrayProxy<vk::ClearValue> clearValues,
                              vk::SubpassContents subpassContents) const {
    vk::CommandBuffer::beginRenderPass(
        vk::RenderPassBeginInfo{renderPass, framebuffer, renderArea, clearValues.size(), clearValues.data()},
        subpassContents);
  }
  void onSubmit(std::shared_ptr<vw::Fence> fence) {
    mState = State::Pending;
    mFence = fence;
  }
  bool isPending() {
    if (mState != State::Pending)
      return false;
    if (mFence->signaled()) {
      mState = State::Invalid;
      mFence.reset();
      return false;
    }
    return true;
  }

 private:
  State mState = State::Initial;
  std::shared_ptr<vw::Fence> mFence;
};

class CommandPool : public vw::HandleContainerUnique<vk::CommandPool> {
 public:
  CommandPool(vk::Device deviceHandle,
              uint32_t queueFamilyIndex,
              vk::CommandPoolCreateFlags flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
      : ContainerType{deviceHandle} {
    mHandle = mDeviceHandle.createCommandPool({flags, queueFamilyIndex});
  }
  void allocateBuffers(uint32_t count);
  size_t getBufferCount() const {
    return mBuffers.size();
  }
  vw::CommandBuffer& operator[](size_t idx) {
    return mBuffers[idx];
  }
  const vw::CommandBuffer& operator[](size_t idx) const {
    return mBuffers[idx];
  }
  auto begin() {
    return mBuffers.begin();
  }
  auto begin() const {
    return mBuffers.begin();
  }
  auto end() {
    return mBuffers.end();
  }
  auto end() const {
    return mBuffers.end();
  }
  inline void reset(bool releaseResources = false) {
    mDeviceHandle.resetCommandPool(
        mHandle, releaseResources ? vk::CommandPoolResetFlagBits::eReleaseResources : vk::CommandPoolResetFlags{});
  }

 private:
  std::vector<vw::CommandBuffer> mBuffers;
};

class Queue : public vw::HandleContainer<vk::Queue> {
 public:
  Queue(vk::Device device, vk::Queue queue, uint32_t queueFamilyIndex)
      : mDeviceHandle{device}, mOneTimeCommandPool{device, queueFamilyIndex}{
        mHandle = queue;
      }
  void allocateOneTimeBuffers(uint32_t count) {
    mOneTimeCommandPool.allocateBuffers(count);
  }
  bool hasReadyBuffer() {
    for (vw::CommandBuffer& buf : mOneTimeCommandPool) {
      if (!buf.isPending())
        return true;
    }
    return false;
  }
  template<typename T>
  std::shared_ptr<vw::Fence> oneTimeRecordSubmit(T recordFunc,
                                                 std::initializer_list<vk::Semaphore> waitSemaphores = {},
                                                 std::initializer_list<vk::PipelineStageFlags> waitStages = {},
                                                 std::initializer_list<vk::Semaphore> signalSemaphores = {}) {
    vw::CommandBuffer& cmdBuff = getReadyOneTimeBuffer();
    cmdBuff.record(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, recordFunc);

    vk::SubmitInfo submitInfo{waitSemaphores, waitStages, cmdBuff, signalSemaphores};
    auto fence = std::make_shared<vw::Fence>(mDeviceHandle);
    cmdBuff.onSubmit(fence);
    mHandle.submit(submitInfo, *fence.get());
    return fence;
  }
  void waitIdle() const {
    mHandle.waitIdle();
  }

 private:
  vw::CommandBuffer& getReadyOneTimeBuffer() {
    for (vw::CommandBuffer& buf : mOneTimeCommandPool) {
      if (!buf.isPending())
        return buf;
    }
    throw std::runtime_error("No recordable command buffers avaliable (hasReadyBuffer() not checked)");
  }
  vk::Device mDeviceHandle;
  vw::CommandPool mOneTimeCommandPool;
};

struct QueueWorkType {
  vk::QueueFlags flags;
  vk::SurfaceKHR surfaceSupport = {};
};

class PhysicalDevice : public vk::PhysicalDevice {
 public:
  PhysicalDevice(vk::PhysicalDevice physicalDevice);
  bool queryWorkTypeSupport(const QueueWorkType& workType) const;
  bool queryExtensionSupport(ArrayProxy<const std::string> extensions) const;

 private:
  std::vector<std::string> mSupportedExtensions;
  std::vector<vk::QueueFamilyProperties> mQueueFamilyProperties;
};

class Instance : public vk::Instance {
 public:
  Instance(std::string appName, uint32_t version, std::vector<const char*> platformExtensions = {});
  ~Instance();
  std::optional<vk::PhysicalDevice> findPhysicalDevice(ArrayProxy<QueueWorkType> workTypes,
                                                       ArrayProxy<const std::string> extensions) const;

 private:
  bool checkValidationLayerSupport();
  bool checkExtensionSupport(std::vector<const char*> extensions);
  std::vector<vw::PhysicalDevice> mPhysicalDevices;
  vk::DebugUtilsMessengerEXT mDebugMessenger;
};

class Device : public vk::Device {
 public:
  Device(vk::PhysicalDevice physicalDevice, ArrayProxy<const std::string> extensions);
  ~Device();
  inline vk::PhysicalDevice getPhysicalDevice() {
    return mPhysicalDeviceHandle;
  };
  inline vw::Queue& getPreferredQueue(const QueueWorkType& workType) {
    return mQueues[getPreferredQueueFamily(workType)];
  }
  void waitIdle();

 private:
  uint32_t getPreferredQueueFamily(const QueueWorkType& workType) const;
  vk::PhysicalDevice mPhysicalDeviceHandle;
  vk::PhysicalDeviceFeatures mDeviceFeatures;

  std::vector<vk::QueueFamilyProperties> mQueueFamilies;
  std::vector<vw::Queue> mQueues;
};
}  // namespace vw