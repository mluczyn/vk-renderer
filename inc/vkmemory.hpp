#pragma once
#include <functional>
#include <future>
#include <vulkan/vulkan.hpp>
#include "vkutils.hpp"

namespace vw {

enum class MemoryPreference {
    GPUIO,
    CPUIO,
    CPUTOGPU
};

struct MemoryRegion {
    vk::DeviceMemory handle;
    vk::DeviceSize offset;
    vk::DeviceSize size;
    vk::DeviceSize padding;
};

class MemoryAllocation {
public:
    MemoryAllocation(const MemoryRegion& region, vk::MemoryPropertyFlags propertyFlags, byte* mappedPtr, std::function<void()> onDestroy);
    MemoryAllocation(MemoryAllocation&& other) noexcept;
    MemoryAllocation(const MemoryAllocation&) = delete;
    MemoryAllocation& operator=(const MemoryAllocation&) = delete;
    ~MemoryAllocation();
    inline const MemoryRegion& getRegion() {
        return mRegion;
    }
    inline byte* getPtr() {
        return mPtr;
    }
    inline bool isDeviceLocal() {
        return mIsDeviceLocal;
    }
    inline bool isHostVisibleCoherent() {
        return mIsHostVisibleCoherent;
    }
private:
    MemoryRegion mRegion;
    bool mIsDeviceLocal, mIsHostVisibleCoherent;
    byte* mPtr;
    std::function<void()> mDestroyFunc;
};

class Buffer {
public:
    Buffer(vk::Device device, vk::Buffer buffer, vk::DeviceSize size, vw::MemoryAllocation memoryAllocation);
    Buffer(Buffer&& other) noexcept;
    ~Buffer();
    inline operator vk::Buffer() const {
        return mBufferHandle;
    }
    template<typename T, typename... Ts>
    auto copyFrom(vk::DeviceSize offset, const T& src, const Ts&... srcs) {
        using SrcValueType = typename T::value_type;
        std::copy(std::begin(src), std::end(src), reinterpret_cast<SrcValueType*>(&(mAllocation.getPtr()[offset])));
        return std::tuple_cat(std::make_tuple(offset), copyFrom(offset + vw::byteSize(src), srcs...));
    }
    auto copyFrom([[maybe_unused]] vk::DeviceSize offset) {
        return std::make_tuple();
    }
    void cmdCopyFrom(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::DeviceSize srcOffset, vk::DeviceSize size, vk::DeviceSize dstOffset = 0) const {
        cmdBuffer.copyBuffer(src, mBufferHandle, vk::BufferCopy{srcOffset, dstOffset, size});
    }
    inline vk::DeviceSize size() const {
        return mSize;
    }
private:
    vk::Device mDeviceHandle;
    vk::DeviceSize mSize;
    vk::Buffer mBufferHandle;
    vw::MemoryAllocation mAllocation;
};

class ImageView : public vw::HandleContainerUnique<vk::ImageView> {
public:
    ImageView(vk::Device device, vk::ImageView handle);
};

class Image {
public:
    Image(vk::Device device, vk::Image image, vk::Format format, vw::MemoryAllocation memoryAllocation);
    Image(Image&& other) noexcept;
    ~Image();
    inline operator vk::Image() const {
        return mImageHandle;
    }
    void transitionLayout(vk::CommandBuffer cmdBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, 
                          vk::ImageSubresourceRange range = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}) const;
    void copyFromBuffer(vk::CommandBuffer cmdBuffer, vk::Buffer src, vk::DeviceSize srcOffset, vk::Extent3D destExtent, vk::Offset3D destOffset = {}, 
                        vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined, vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                        vk::ImageSubresourceLayers layers = {vk::ImageAspectFlagBits::eColor, 0, 0, 1}) const;
    vw::ImageView createView(vk::ImageViewType viewType = vk::ImageViewType::e2D,  
                             vk::ImageSubresourceRange range = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}, 
                             vk::ComponentMapping components = {}) const;
private:
    vk::Device mDeviceHandle;
    vk::Image mImageHandle;
    vk::Format mFormat;
    vw::MemoryAllocation mAllocation;
};

class MemoryAllocator {
public:
    MemoryAllocator(vk::PhysicalDevice physicalDevice, vk::Device device, vk::DeviceSize pageSize = 1024 * 1024);
    ~MemoryAllocator();
    vw::Buffer createBuffer(vk::DeviceSize size, vk::BufferUsageFlags usageFlags, vw::MemoryPreference memoryPreference);
    vw::Image createImage(vk::Format format, vk::Extent3D dimensions, vk::ImageUsageFlags usage, vw::MemoryPreference memoryPreference, 
                          vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1, vk::ImageType type = vk::ImageType::e2D, uint32_t mipLevels = 1, uint32_t arrayLayers = 1);
    inline auto createBufferTask(vk::DeviceSize size, vk::BufferUsageFlags usageFlags, vw::MemoryPreference memoryPreference) {
        return std::packaged_task<std::shared_ptr<vw::Buffer>()>{[=] {
            return std::make_shared<vw::Buffer>(createBuffer(size, usageFlags, memoryPreference));
        }};
    }
private:
    MemoryAllocation allocate(const vk::MemoryRequirements& requirements, MemoryPreference preference);
    MemoryAllocation findRegion(uint32_t memoryType, vk::DeviceSize size, vk::DeviceSize alignment);
    void freeRegion(uint32_t memoryType, size_t blockIndex, const MemoryRegion& region);
    vk::Device mDeviceHandle;
    vk::PhysicalDevice mPhysicalDeviceHandle;
    vk::PhysicalDeviceMemoryProperties mMemoryProperties;
    struct MemoryBlock {
        struct BlockRegion {
            vk::DeviceSize offset;
            vk::DeviceSize size;
        };
        vk::DeviceMemory handle;
        vk::DeviceSize size;
        std::vector<BlockRegion> freeRegions;
        byte* mappedPtr = nullptr;
    };
    std::vector<std::vector<MemoryBlock>> mPools;
    vk::DeviceSize mPageSize;
    uint32_t mDeviceLocalMask = 0, mHostVisibleCoherentMask = 0;
    std::vector<std::mutex> mPoolMutexes; //synchronize mPools access
};

};