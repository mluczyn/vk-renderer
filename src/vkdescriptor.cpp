#include "vkdescriptor.hpp"

vw::DescriptorSetLayout::DescriptorSetLayout(vk::Device device,
                                             ArrayProxy<vk::DescriptorSetLayoutBinding> layoutBindings,
                                             bool hasVariable)
    : ContainerType{device}, mLayoutBindings{layoutBindings.begin(), layoutBindings.end()} {
  vk::DescriptorSetLayoutCreateInfo createInfo{{}, layoutBindings.size(), layoutBindings.data()};
  std::vector<vk::DescriptorBindingFlags> bindingFlags(layoutBindings.size(), vk::DescriptorBindingFlags{});
  if (hasVariable)
    bindingFlags.back() = vk::DescriptorBindingFlagBits::eVariableDescriptorCount;
  vk::DescriptorSetLayoutBindingFlagsCreateInfo createFlagsInfo{static_cast<uint32_t>(bindingFlags.size()),
                                                                bindingFlags.data()};
  createInfo.pNext = &createFlagsInfo;
  mHandle = mDeviceHandle.createDescriptorSetLayout(createInfo);
}

vw::DescriptorPool::DescriptorPool(vk::Device device, uint32_t maxSets, ArrayProxy<vk::DescriptorPoolSize> poolSizes)
    : ContainerType{device} {
  mHandle = mDeviceHandle.createDescriptorPool({{}, maxSets, poolSizes.size(), poolSizes.data()});
}

vw::DedicatedDescriptorPool::DedicatedDescriptorPool(vk::Device device,
                                                     uint32_t setCount,
                                                     vk::DescriptorSetLayout layout,
                                                     ArrayProxy<vk::DescriptorSetLayoutBinding> bindings,
                                                     uint32_t variableDescriptorCount)
    : mDeviceHandle{device} {
  mPoolSizes.reserve(bindings.size());
  for (auto&& binding : bindings)
    mPoolSizes.emplace_back(binding.descriptorType, binding.descriptorCount * setCount);

  mPool.emplace(mDeviceHandle, setCount, mPoolSizes);

  std::vector<vk::DescriptorSetLayout> layoutHandles{setCount, layout};
  vk::DescriptorSetAllocateInfo allocateInfo{*mPool, setCount, layoutHandles.data()};
  std::vector<uint32_t> variableDescCounts(setCount, variableDescriptorCount);
  vk::DescriptorSetVariableDescriptorCountAllocateInfo variableAllocateInfo{variableDescriptorCount ? setCount : 0,
                                                                            variableDescCounts.data()};
  allocateInfo.pNext = &variableAllocateInfo;
  mSetHandles = mDeviceHandle.allocateDescriptorSets(allocateInfo);
}