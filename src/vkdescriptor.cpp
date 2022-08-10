#include "vkdescriptor.hpp"

vw::DescriptorSetLayout::DescriptorSetLayout(ArrayProxy<vk::DescriptorSetLayoutBinding> layoutBindings, bool hasVariable)
    : mLayoutBindings{layoutBindings.begin(), layoutBindings.end()} {
  vk::DescriptorSetLayoutCreateInfo createInfo{{}, layoutBindings.size(), layoutBindings.data()};

  std::vector<vk::DescriptorBindingFlags> bindingFlags(layoutBindings.size(), vk::DescriptorBindingFlags{});

  if (hasVariable)
    bindingFlags.back() = vk::DescriptorBindingFlagBits::eVariableDescriptorCount;

  vk::DescriptorSetLayoutBindingFlagsCreateInfo createFlagsInfo{static_cast<uint32_t>(bindingFlags.size()), bindingFlags.data()};

  createInfo.pNext = &createFlagsInfo;
  mHandle = vw::g::device.createDescriptorSetLayout(createInfo);
}

vw::DescriptorPool::DescriptorPool(uint32_t maxSets, ArrayProxy<vk::DescriptorPoolSize> poolSizes) {
  mHandle = vw::g::device.createDescriptorPool({{}, maxSets, poolSizes.size(), poolSizes.data()});
}

vw::DedicatedDescriptorPool::DedicatedDescriptorPool(uint32_t setCount,
                                                     vk::DescriptorSetLayout layout,
                                                     ArrayProxy<vk::DescriptorSetLayoutBinding> bindings,
                                                     uint32_t variableDescriptorCount) {
  mPoolSizes.reserve(bindings.size());
  for (auto&& binding : bindings)
    mPoolSizes.emplace_back(binding.descriptorType, binding.descriptorCount * setCount);

  mPool.emplace(setCount, mPoolSizes);

  std::vector<vk::DescriptorSetLayout> layoutHandles{setCount, layout};
  vk::DescriptorSetAllocateInfo allocateInfo{*mPool, setCount, layoutHandles.data()};
  std::vector<uint32_t> variableDescCounts(setCount, variableDescriptorCount);
  vk::DescriptorSetVariableDescriptorCountAllocateInfo variableAllocateInfo{variableDescriptorCount ? setCount : 0, variableDescCounts.data()};
  allocateInfo.pNext = &variableAllocateInfo;
  auto setHandles = vw::g::device.allocateDescriptorSets(allocateInfo);
  mSetHandles.reserve(setHandles.size());
  for (auto set : setHandles)
    mSetHandles.emplace_back(set);
}

vw::PipelineLayout::PipelineLayout(vw::ArrayProxy<std::reference_wrapper<vw::Shader>> shaders) {
  uint32_t setCount = 1;
  for (auto& shader : shaders) {
    for (auto& ioBinding : shader.get().getIOBindings())
      setCount = std::max(setCount, ioBinding.setIndex + 1);
  }

  uint32_t pushOffset = 0;
  std::vector<uint32_t> bindingIndices(setCount);
  std::vector<std::vector<vk::DescriptorSetLayoutBinding>> layoutBindings(setCount);
  std::vector<vk::PushConstantRange> pushRanges;
  std::vector<bool> hasVariableDesc(setCount);

  for (auto& shader : shaders) {
    auto stage = shader.get().getStage();
    for (auto& ioBinding : shader.get().getIOBindings()) {
      uint32_t setIndex = ioBinding.setIndex;
      layoutBindings[setIndex].emplace_back(bindingIndices[setIndex]++, ioBinding.type, ioBinding.count, stage);
      hasVariableDesc[setIndex] = hasVariableDesc[setIndex] || ioBinding.isVariable;
    }
    uint32_t pushSize = shader.get().getPushConstantSize();
    if (pushSize > 0) {
      pushRanges.emplace_back(stage, pushOffset, pushSize);
      pushOffset += pushSize;
    }
  }

  for (size_t i = 0; i < setCount; ++i)
    mDescriptorLayouts.emplace_back(layoutBindings[i], hasVariableDesc[i]);

  std::vector<vk::DescriptorSetLayout> setLayoutHandles;
  setLayoutHandles.reserve(setCount);
  for (auto& setLayout : mDescriptorLayouts)
    setLayoutHandles.push_back(setLayout);

  mHandle = vw::g::device.createPipelineLayout({{}, vw::size32(setLayoutHandles), setLayoutHandles.data(), vw::size32(pushRanges), pushRanges.data()});
}