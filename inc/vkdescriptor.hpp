#pragma once

#include "vkcore.hpp"

namespace vw {
class DescriptorPool : public vw::HandleContainerUnique<vk::DescriptorPool> {
 public:
  DescriptorPool(vk::Device device, uint32_t maxSets, ArrayProxy<vk::DescriptorPoolSize> poolSizes);
};

class DedicatedDescriptorPool {
 public:
  DedicatedDescriptorPool(vk::Device device,
                          uint32_t setCount,
                          vk::DescriptorSetLayout layout,
                          ArrayProxy<vk::DescriptorSetLayoutBinding> bindings,
                          uint32_t variableDescriptorCount = 0);
  std::vector<vk::DescriptorSet>& getSets() {
    return mSetHandles;
  }

 private:
  vk::Device mDeviceHandle;
  std::vector<vk::DescriptorPoolSize> mPoolSizes;
  std::optional<vw::DescriptorPool> mPool;
  std::vector<vk::DescriptorSet> mSetHandles;
};

class DescriptorSetLayout : public vw::HandleContainerUnique<vk::DescriptorSetLayout> {
 public:
  DescriptorSetLayout(vk::Device device, ArrayProxy<vk::DescriptorSetLayoutBinding> layoutBindings, bool hasVariable = false);
  inline DedicatedDescriptorPool createDedicatedPool(uint32_t setCount, uint32_t variableDescriptorCount = 0) const {
    return DedicatedDescriptorPool{mDeviceHandle, setCount, mHandle, mLayoutBindings, variableDescriptorCount};
  }

 private:
  std::vector<vk::DescriptorSetLayoutBinding> mLayoutBindings;
};
};  // namespace vw