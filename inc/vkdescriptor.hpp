#pragma once

#include "vkcore.hpp"
#include "vkshader.hpp"

namespace vw {

class DescriptorSet : public vw::HandleContainer<vk::DescriptorSet> {
 public:
  DescriptorSet(vk::DescriptorSet handle) {
    mHandle = handle;
  }
  vk::WriteDescriptorSet writeImages(uint32_t dstBinding,
                                     vk::DescriptorType type,
                                     vw::ArrayProxy<vk::DescriptorImageInfo> imageInfos,
                                     uint32_t arrayOffset = 0) {
    return vk::WriteDescriptorSet{mHandle, dstBinding, arrayOffset, imageInfos.size(), type, imageInfos.data()};
  }
  vk::WriteDescriptorSet writeBuffers(uint32_t dstBinding,
                                      vk::DescriptorType type,
                                      vw::ArrayProxy<vk::DescriptorBufferInfo> bufferInfos,
                                      uint32_t arrayOffset = 0) {
    return vk::WriteDescriptorSet{mHandle, dstBinding, arrayOffset, bufferInfos.size(), type, nullptr, bufferInfos.data()};
  }
};
class DescriptorPool : public vw::HandleContainerUnique<vk::DescriptorPool> {
 public:
  DescriptorPool(uint32_t maxSets, ArrayProxy<vk::DescriptorPoolSize> poolSizes);
};

class DedicatedDescriptorPool {
 public:
  DedicatedDescriptorPool(uint32_t setCount,
                          vk::DescriptorSetLayout layout,
                          ArrayProxy<vk::DescriptorSetLayoutBinding> bindings,
                          uint32_t variableDescriptorCount = 0);
  std::vector<vw::DescriptorSet>& getSets() {
    return mSetHandles;
  }

 private:
  std::vector<vk::DescriptorPoolSize> mPoolSizes;
  std::optional<vw::DescriptorPool> mPool;
  std::vector<vw::DescriptorSet> mSetHandles;
};

class DescriptorSetLayout : public vw::HandleContainerUnique<vk::DescriptorSetLayout> {
 public:
  DescriptorSetLayout(ArrayProxy<vk::DescriptorSetLayoutBinding> layoutBindings, bool hasVariable = false);
  DedicatedDescriptorPool createDedicatedPool(uint32_t setCount, uint32_t variableDescriptorCount = 0) const {
    return DedicatedDescriptorPool{setCount, mHandle, mLayoutBindings, variableDescriptorCount};
  }

 private:
  std::vector<vk::DescriptorSetLayoutBinding> mLayoutBindings;
};

class PipelineLayout : public vw::HandleContainerUnique<vk::PipelineLayout> {
 public:
  PipelineLayout(vw::ArrayProxy<std::reference_wrapper<vw::Shader>> shaders);
  const std::vector<vw::DescriptorSetLayout>& getDescLayouts() const {
    return mDescriptorLayouts;
  }

 private:
  std::vector<vw::DescriptorSetLayout> mDescriptorLayouts;
};
};  // namespace vw