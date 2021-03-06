#include "vulkan/vulkan.hpp"
#include "..\inc\vkmemory.hpp"
#include <exception>

int32_t findProperties(const vk::PhysicalDeviceMemoryProperties& memoryProperties, uint32_t memoryTypeBitsRequirement, vk::MemoryPropertyFlags requiredProperties) {
    const uint32_t memoryCount = memoryProperties.memoryTypeCount;
    for (uint32_t memoryIndex = 0; memoryIndex < memoryCount; ++memoryIndex) {
        const bool isRequiredMemoryType = memoryTypeBitsRequirement & (1 << memoryIndex);
        const bool hasRequiredProperties = (memoryProperties.memoryTypes[memoryIndex].propertyFlags & requiredProperties) == requiredProperties;

        if (isRequiredMemoryType && hasRequiredProperties)
            return static_cast<int32_t>(memoryIndex);
    }
    // failed to find memory type
    return -1;
}

vw::MemoryAllocation::MemoryAllocation(const MemoryRegion& region, vk::MemoryPropertyFlags propertyFlags, byte* mappedPtr, std::function<void()> onDestroy)
: mRegion{region}, mPtr{mappedPtr}, mDestroyFunc{onDestroy} {
    mIsDeviceLocal = static_cast<bool>(propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal);
    mIsHostVisibleCoherent = static_cast<bool>(propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) && static_cast<bool>(propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent);
}

vw::MemoryAllocation::MemoryAllocation(MemoryAllocation&& other) noexcept : mRegion{other.mRegion}, mIsDeviceLocal{other.mIsDeviceLocal},
mIsHostVisibleCoherent{other.mIsHostVisibleCoherent}, mPtr{other.mPtr}, mDestroyFunc{std::move(other.mDestroyFunc)} {
    other.mDestroyFunc = []{};
}

vw::MemoryAllocation::~MemoryAllocation() {
    mDestroyFunc();
}

vw::MemoryAllocator::MemoryAllocator(vk::PhysicalDevice physicalDevice, vk::Device device, vk::DeviceSize pageSize) 
: mDeviceHandle{device}, mPhysicalDeviceHandle{physicalDevice}, mMemoryProperties{physicalDevice.getMemoryProperties()}, 
  mPageSize{pageSize}, mPools{mMemoryProperties.memoryTypeCount}, mPoolMutexes{mMemoryProperties.memoryTypeCount} {
    for (size_t i = 0; i < mMemoryProperties.memoryTypeCount; ++i) {
        auto propFlags = mMemoryProperties.memoryTypes[i].propertyFlags;
        auto hasFlag = [=](vk::MemoryPropertyFlags flag) -> bool {return static_cast<bool>(propFlags & flag); };

        mDeviceLocalMask |= hasFlag(vk::MemoryPropertyFlagBits::eDeviceLocal) << i;
        mHostVisibleCoherentMask |= (hasFlag(vk::MemoryPropertyFlagBits::eHostVisible) && hasFlag(vk::MemoryPropertyFlagBits::eHostCoherent)) << i;
    }
}

vw::MemoryAllocator::~MemoryAllocator() {
    for (auto& pool : mPools) {
        for (auto& block : pool)
            mDeviceHandle.freeMemory(block.handle);
    }
}

vw::Buffer vw::MemoryAllocator::createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usageFlags, vw::MemoryPreference memoryPreference) {
    vk::BufferCreateInfo createInfo;
    createInfo.size = size;
    createInfo.usage = usageFlags;
    createInfo.sharingMode = vk::SharingMode::eExclusive;
    vk::Buffer buffer = mDeviceHandle.createBuffer(createInfo);

    return vw::Buffer{mDeviceHandle, buffer, size, allocate(mDeviceHandle.getBufferMemoryRequirements(buffer), memoryPreference)};
}

vw::Image vw::MemoryAllocator::createImage(vk::Format format, vk::Extent3D dimensions, vk::ImageUsageFlags usage, vw::MemoryPreference memoryPreference,
                                           vk::SampleCountFlagBits samples, vk::ImageType type, uint32_t mipLevels, uint32_t arrayLayers) {
    vk::ImageCreateInfo createInfo;
    createInfo.imageType = type;
    createInfo.format = format;
    createInfo.extent = dimensions;
    createInfo.mipLevels = mipLevels;
    createInfo.arrayLayers = arrayLayers;
    createInfo.samples = samples;
    createInfo.tiling = vk::ImageTiling::eOptimal;
    createInfo.usage = usage;
    createInfo.initialLayout = vk::ImageLayout::eUndefined;
    vk::Image image = mDeviceHandle.createImage(createInfo);

    return vw::Image{mDeviceHandle, image, format, allocate(mDeviceHandle.getImageMemoryRequirements(image), memoryPreference)};
}

vw::MemoryAllocation vw::MemoryAllocator::allocate(const vk::MemoryRequirements& requirements, vw::MemoryPreference preference) {
    uint32_t memoryBits = requirements.memoryTypeBits;
    auto applyMask = [&](uint32_t mask) -> void {
        if (memoryBits & mask)
            memoryBits &= mask;
    };

    if (preference == vw::MemoryPreference::CPUIO || preference == vw::MemoryPreference::CPUTOGPU)
        applyMask(mHostVisibleCoherentMask);
    if (preference == vw::MemoryPreference::GPUIO || preference == vw::MemoryPreference::CPUTOGPU)
        applyMask(mDeviceLocalMask);
    
    uint32_t memoryType = 0;
    while (!((memoryBits >> memoryType) & 1))
        memoryType++;
    return findRegion(memoryType, requirements.size, requirements.alignment);
}

void vw::MemoryAllocator::freeRegion(uint32_t memoryType, size_t blockIndex, const MemoryRegion& region) {
    std::scoped_lock slck{mPoolMutexes[memoryType]};

    auto& block = mPools[memoryType][blockIndex];
    for (size_t i = 0; i < block.freeRegions.size(); ++i) {
        auto& iRegion = block.freeRegions[i];
        vk::DeviceSize regionStart = region.offset - region.padding, regionEnd = region.offset + region.size, regionSize = region.size + region.padding;
        //preceding region found
        if ((iRegion.offset + iRegion.size) == regionStart) {
        //succeeding region also found
        // | i | allocation | i+1 | -> | i |
            if (i + 1 < block.freeRegions.size() && block.freeRegions[i + 1].offset == regionEnd) {
                iRegion.size += regionSize + block.freeRegions[i + 1].size;
                block.freeRegions.erase(block.freeRegions.begin() + i + 1);
            }
            // | i | allocation | -> | i |
            else
                iRegion.size += regionSize;
            return;
        }
        //succeeding region found
        // | allocation | i | -> | i |
        else if (iRegion.offset == regionEnd) {
            iRegion.size += regionSize;
            iRegion.offset = regionStart;
            return;
        }
        //if no adjacent regions, insert before the next region offset-wise
        else if (iRegion.offset > regionEnd) {
            block.freeRegions.insert(block.freeRegions.begin() + i, {regionStart, regionSize});
            return;
        }
        block.freeRegions.push_back({regionStart, regionSize});
        return;
    }
}

vw::MemoryAllocation vw::MemoryAllocator::findRegion(uint32_t memoryType, vk::DeviceSize size, vk::DeviceSize alignment) {
    std::scoped_lock slck{mPoolMutexes[memoryType]};
    for (size_t i = 0; i < mPools[memoryType].size(); ++i) {
        auto& block = mPools[memoryType][i];
        for (size_t j = 0; j < block.freeRegions.size(); ++j) {
            auto& region = block.freeRegions[j];
            auto padding = region.offset % alignment;
            if(padding)
                padding = alignment - padding;
            auto paddedSize = size + padding;
            if (region.size >= paddedSize) {
                const vk::DeviceSize offset = region.offset + padding;

                region.offset += paddedSize;
                region.size -= paddedSize;
                
                if (region.size == 0)
                    block.freeRegions.erase(block.freeRegions.begin() + j);

                byte* alignedPtr = &block.mappedPtr[offset];
                vw::MemoryRegion memoryRegion{block.handle, offset, size, padding};
                return vw::MemoryAllocation{
                    memoryRegion,
                    mMemoryProperties.memoryTypes[memoryType].propertyFlags,
                    alignedPtr,
                    [=]() {
                        freeRegion(memoryType, i, memoryRegion);
                    }};
            }
        }
    }

    vk::MemoryAllocateInfo allocateInfo;
    allocateInfo.memoryTypeIndex = memoryType;
    allocateInfo.allocationSize = ((size / mPageSize) + static_cast<bool>(size % mPageSize)) * mPageSize;
    auto allocationHandle = mDeviceHandle.allocateMemory(allocateInfo);
    
    auto propertyFlags = mMemoryProperties.memoryTypes[memoryType].propertyFlags;
    byte* allocationPtr = 0;
    if (propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible)
        allocationPtr = static_cast<byte*>(mDeviceHandle.mapMemory(allocationHandle, 0, allocateInfo.allocationSize));

    std::vector<MemoryBlock::BlockRegion> freeRegions;
    if (allocateInfo.allocationSize != size)
        freeRegions.push_back({size, allocateInfo.allocationSize - size});
    mPools[memoryType].push_back({allocationHandle, allocateInfo.allocationSize, std::move(freeRegions), allocationPtr});

    vw::MemoryRegion memoryRegion = {mPools[memoryType].back().handle, 0, size, 0};
    auto blockIndex = mPools[memoryType].size() - 1;
    return vw::MemoryAllocation(memoryRegion, propertyFlags, allocationPtr, 
        [=]() {
            freeRegion(memoryType, blockIndex, memoryRegion);
        });

}

vw::Buffer::Buffer(vk::Device device, vk::Buffer buffer, vk::DeviceSize size, vw::MemoryAllocation memoryAllocation)
: mDeviceHandle{device}, mBufferHandle{buffer}, mSize{size}, mAllocation{std::move(memoryAllocation)} {
    //TODO: use bindBufferMemory2
    mDeviceHandle.bindBufferMemory(mBufferHandle, mAllocation.getRegion().handle, mAllocation.getRegion().offset);
}

vw::Buffer::Buffer(Buffer&& other) noexcept : mDeviceHandle{other.mDeviceHandle}, mSize{other.mSize}, mBufferHandle{other.mBufferHandle},
mAllocation{std::move(other.mAllocation)} {}

vw::Buffer::~Buffer() {
    mDeviceHandle.destroy(mBufferHandle);
}

vw::Image::Image(vk::Device device, vk::Image image, vk::Format format, vw::MemoryAllocation memoryAllocation)
: mDeviceHandle{device}, mImageHandle{image}, mFormat{format}, mAllocation{std::move(memoryAllocation)} {
    mDeviceHandle.bindImageMemory(mImageHandle, mAllocation.getRegion().handle, mAllocation.getRegion().offset);
}

vw::Image::Image(Image&& other) noexcept
: mDeviceHandle{other.mDeviceHandle}, mImageHandle{other.mImageHandle}, mFormat{other.mFormat}, mAllocation{std::move(other.mAllocation)} {}

vw::Image::~Image() {
    mDeviceHandle.destroy(mImageHandle);
}

const static std::unordered_map<vk::ImageLayout, vk::AccessFlags> MAP_LAYOUT_TO_ACCESS_FLAGS{
    {vk::ImageLayout::eUndefined, {}},
    {vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eTransferWrite},
    {vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead},
    {vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead}
};

const static std::unordered_map<vk::ImageLayout, vk::PipelineStageFlags> MAP_LAYOUT_TO_STAGE{
    {vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits::eTopOfPipe},
    {vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eTransfer},
    {vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eFragmentShader},
    {vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::PipelineStageFlagBits::eEarlyFragmentTests}
};

void vw::Image::transitionLayout(vk::CommandBuffer cmdBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageSubresourceRange range) const {
    vk::ImageMemoryBarrier barrier;
    vk::PipelineStageFlags srcStageMask, dstStageMask;

    try {
        barrier.srcAccessMask = MAP_LAYOUT_TO_ACCESS_FLAGS.at(oldLayout);
        barrier.dstAccessMask = MAP_LAYOUT_TO_ACCESS_FLAGS.at(newLayout);
        srcStageMask = MAP_LAYOUT_TO_STAGE.at(oldLayout);
        dstStageMask = MAP_LAYOUT_TO_STAGE.at(newLayout);
    } catch(std::out_of_range&) {
        throw std::runtime_error("Unsupported layout transition!");
    }

    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.image = mImageHandle;
    barrier.subresourceRange = range;

    cmdBuffer.pipelineBarrier(srcStageMask, dstStageMask, {}, {}, {}, barrier);
}

void vw::Image::copyFromBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::DeviceSize srcOffset, 
                               vk::Extent3D destExtent, vk::Offset3D destOffset, vk::ImageLayout initialLayout, 
                               vk::ImageLayout finalLayout, vk::ImageSubresourceLayers layers) const {
    transitionLayout(cmdBuffer, initialLayout, vk::ImageLayout::eTransferDstOptimal);
    vk::BufferImageCopy copyInfo;
    copyInfo.bufferOffset = srcOffset;
    copyInfo.imageExtent = destExtent;
    copyInfo.imageOffset = destOffset;
    copyInfo.imageSubresource = layers;
    cmdBuffer.copyBufferToImage(src, mImageHandle, vk::ImageLayout::eTransferDstOptimal, copyInfo);
    transitionLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, finalLayout);
}

vw::ImageView vw::Image::createView(vk::ImageViewType viewType, vk::ImageSubresourceRange range, vk::ComponentMapping components) const {
    vk::ImageViewCreateInfo createInfo;
    createInfo.image = mImageHandle;
    createInfo.viewType = viewType;
    createInfo.format = mFormat;
    createInfo.components = components;
    createInfo.subresourceRange = range;
    return vw::ImageView{mDeviceHandle, mDeviceHandle.createImageView(createInfo)};
}

vw::ImageView::ImageView(vk::Device device, vk::ImageView handle) : ContainerType{device} {
    mHandle = handle;
}
