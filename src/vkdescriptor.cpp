#include "vkdescriptor.hpp"

vw::DescriptorSetLayout::DescriptorSetLayout(vk::Device device, ArrayProxy<vk::DescriptorSetLayoutBinding> layoutBindings)
: ContainerType{device}, mLayoutBindings{layoutBindings.begin(), layoutBindings.end()} {
    mHandle = mDeviceHandle.createDescriptorSetLayout({
        {},
        layoutBindings.size(),
        layoutBindings.data()
        });
}

vw::DescriptorPool::DescriptorPool(vk::Device device, uint32_t maxSets, ArrayProxy<vk::DescriptorPoolSize> poolSizes)
: ContainerType{device} {
    mHandle = mDeviceHandle.createDescriptorPool({
        {},
        maxSets,
        poolSizes.size(),
        poolSizes.data()
        });
}

vw::DedicatedDescriptorPool::DedicatedDescriptorPool(vk::Device device, uint32_t setCount, vk::DescriptorSetLayout layout, ArrayProxy<vk::DescriptorSetLayoutBinding> bindings)
: mDeviceHandle{device} {
    mPoolSizes.reserve(bindings.size());
    for(auto&& binding : bindings)
        mPoolSizes.emplace_back(binding.descriptorType, binding.descriptorCount * setCount);

    mPool.emplace(mDeviceHandle, setCount, mPoolSizes);

    std::vector<vk::DescriptorSetLayout> layoutHandles{setCount, layout};
    mSetHandles = mDeviceHandle.allocateDescriptorSets({
        *mPool,
        setCount,
        layoutHandles.data()
    });
}