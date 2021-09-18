#pragma once
#include <functional>
#include <future>
#include <vulkan/vulkan.hpp>
#include "vkcore.hpp"
#include "vkutils.hpp"

namespace vw {

constexpr vk::ImageSubresourceLayers kDefaultImageLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};

enum class MemoryPreference { GPUIO, CPUIO, CPUTOGPU };

namespace BufferUse {
constexpr vk::BufferUsageFlags kStagingBuffer = vk::BufferUsageFlagBits::eTransferSrc;
constexpr vk::BufferUsageFlags kUniformBuffer = vk::BufferUsageFlagBits::eUniformBuffer;
constexpr vk::BufferUsageFlags kStorageBuffer = vk::BufferUsageFlagBits::eStorageBuffer;
constexpr vk::BufferUsageFlags kVertexBuffer = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
constexpr vk::BufferUsageFlags kIndexBuffer = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
}

namespace ImageUse {
constexpr vk::ImageUsageFlags kTexture = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
};

struct MemoryRegion {
  vk::DeviceMemory handle;
  vk::DeviceSize offset;
  vk::DeviceSize size;
  vk::DeviceSize padding;
};

class MemoryAllocation {
 public:
  MemoryAllocation(MemoryRegion& region, vk::MemoryPropertyFlags propertyFlags, std::byte* mappedPtr, std::function<void(MemoryRegion*)> deleter)
      : mRegionPtr{&region, deleter}, mPtr{mappedPtr} {
     mIsDeviceLocal = static_cast<bool>(propertyFlags & vk::MemoryPropertyFlagBits::eDeviceLocal);
     mIsHostVisibleCoherent = (propertyFlags & vk::MemoryPropertyFlagBits::eHostVisible) && (propertyFlags & vk::MemoryPropertyFlagBits::eHostCoherent);
   }
  inline MemoryRegion& getRegion() {
    return *mRegionPtr.get();
  }
  inline const MemoryRegion& getRegion() const {
    return *mRegionPtr.get();
  }
  inline std::byte* getPtr() {
    return mPtr;
  }
  inline bool isDeviceLocal() {
    return mIsDeviceLocal;
  }
  inline bool isHostVisibleCoherent() {
    return mIsHostVisibleCoherent;
  }

 private:
  std::unique_ptr<MemoryRegion, std::function<void(MemoryRegion*)>> mRegionPtr;
  bool mIsDeviceLocal, mIsHostVisibleCoherent;
  std::byte* mPtr;
};

class MemoryAllocator {
 public:
  virtual ~MemoryAllocator() = default;
  virtual MemoryAllocation allocate(const vk::MemoryRequirements& requirements, MemoryPreference preference) = 0;
};

class Buffer : public vw::HandleContainerUnique<vk::Buffer> {
 public:
  Buffer(vk::Device device, MemoryAllocator& allocator, vk::DeviceSize size, vk::BufferUsageFlags usage, vw::MemoryPreference memoryPreference = vw::MemoryPreference::GPUIO);
  inline vk::DeviceSize size() const {
    return mSize;
  }
  template<typename T>
  void copyToMapped(const T& src, vk::DeviceSize offset = 0) {
    assert(mAllocation->getPtr() != nullptr);
    vk::DeviceSize srcSize = vw::byteSize(src);
    assert(srcSize + offset <= mSize);
    std::copy(std::begin(src), std::end(src), reinterpret_cast<typename T::value_type*>(&(mAllocation->getPtr()[offset])));
  }
 protected:
  vk::DeviceSize mSize;
  std::optional<vw::MemoryAllocation> mAllocation;
};

class ImageView : public vw::HandleContainerUnique<vk::ImageView> {
 public:
  ImageView(vk::Device device, vk::ImageView handle);
};

class Image : public vw::HandleContainerUnique<vk::Image> {
 public:
  static constexpr vk::ImageSubresourceRange kDefaultSubResourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
  Image(vk::Device device,
    MemoryAllocator& allocator,
    vk::Format format,
    vk::Extent3D extent,
    vk::ImageUsageFlags usage,
    vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
    vk::ImageType type = vk::ImageType::e2D,
    uint32_t mipLevels = 1,
    uint32_t arrayLayers = 1,
    vw::MemoryPreference memoryPreference = vw::MemoryPreference::GPUIO);
  static void transitionLayout(vk::CommandBuffer cmdBuffer,
                                          vk::Image image,
                                          vk::ImageLayout oldLayout,
                                          vk::ImageLayout newLayout,
                                          vk::ImageSubresourceRange range = kDefaultSubResourceRange);
  void transitionLayout(vk::CommandBuffer cmdBuffer,
                        vk::ImageLayout oldLayout,
                        vk::ImageLayout newLayout,
                        vk::ImageSubresourceRange range = kDefaultSubResourceRange) const;
  void copyFromBuffer(vk::CommandBuffer cmdBuffer,
                      vk::Buffer src,
                      vk::DeviceSize srcOffset,
                      vk::Extent3D destExtent,
                      vk::Offset3D destOffset = {},
                      vk::ImageLayout initialLayout = vk::ImageLayout::eUndefined,
                      vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                      vk::ImageSubresourceLayers layers = {vk::ImageAspectFlagBits::eColor, 0, 0, 1}) const;
  vw::ImageView createView(vk::ImageViewType viewType = vk::ImageViewType::e2D,
                           vk::ImageSubresourceRange range = kDefaultSubResourceRange,
                           vk::ComponentMapping components = {}) const;

 private:
  vk::Extent3D mExtent;
  vk::Format mFormat;
  std::optional<vw::MemoryAllocation> mAllocation;
};

class StagingBuffer : public vw::Buffer {
 public:
  StagingBuffer(vk::Device device, vw::MemoryAllocator& allocator, vk::DeviceSize size, vw::Queue& transferQueue)
      : vw::Buffer{device, allocator, size, vw::BufferUse::kStagingBuffer, vw::MemoryPreference::CPUTOGPU},
        mTransferQueue{transferQueue} {}
  template <typename T>
  void queueBufferCopy(const T& src, vk::Buffer dst, vk::DeviceSize dstOffset = 0) {
    vk::DeviceSize dataSize = vw::byteSize(src);
    if(dataSize > mSize)
      throw std::runtime_error("Data size exceeds staging buffer size");
    if(dataSize > remainingSpace())
      flush();
    vk::DeviceSize srcOffset = copyToMappedEnd(src);
    mStagedBufferCopies.push_back({dst, vk::BufferCopy{srcOffset, dstOffset, vw::byteSize(src)}});
  }
  template <typename T>
  void queueBufferCopies(const T &srcs, vk::Buffer dst, vk::DeviceSize baseDstOffset = 0) {
    vk::DeviceSize totalSize = 0;
    for (const auto& src : srcs)
      totalSize += vw::byteSize(src);

    if(totalSize > mSize) {
      vk::DeviceSize dstOffset = baseDstOffset;
      for(const auto& src : srcs) {
        queueBufferCopy(src, dst, dstOffset);
        dstOffset += vw::byteSize(src);
      }
    } else {
      if(totalSize > remainingSpace())
        flush();
    vk::DeviceSize srcOffset = mUsedBytes;
    for (const auto& src : srcs)
      (void)copyToMappedEnd(src);
    mStagedBufferCopies.push_back({dst, vk::BufferCopy{srcOffset, baseDstOffset, totalSize}});
    }
  }
  void queueImageCopy(const ImageFile& imageFile,
                      vk::Image dst,
                      vk::ImageLayout preLayout = vk::ImageLayout::eUndefined,
                      vk::ImageLayout postLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
                      vk::ImageSubresourceLayers layers = kDefaultImageLayers,
                      vk::Offset3D destOffset = {}) {
    // Align to 16 bytes (texel size)
    mUsedBytes = (mUsedBytes + 15) & ~15;
    vk::DeviceSize imageDataSize = imageFile.dataSize();
    if (imageDataSize > mSize)
      throw std::runtime_error("Image data size exceeds staging buffer size");
    if (imageDataSize > remainingSpace())
      flush();
    vk::DeviceSize srcOffset = mUsedBytes;
    imageFile.loadData(mAllocation->getPtr() + srcOffset);
    mUsedBytes += imageDataSize;

    vk::BufferImageCopy copyInfo;
    copyInfo.bufferOffset = srcOffset;
    copyInfo.imageExtent = imageFile.getExtent();
    copyInfo.imageOffset = destOffset;
    copyInfo.imageSubresource = layers;
    mStagedImageCopies.push_back({dst, preLayout, postLayout, copyInfo});
  }
  vk::DeviceSize remainingSpace() const {
    return mSize - mUsedBytes;
  }
  void flush() {
    auto fence = mTransferQueue.oneTimeRecordSubmit([&](vw::CommandBuffer& cmdBuffer){
      for (const auto& copy : mStagedBufferCopies) {
        cmdBuffer.copyBuffer(mHandle, copy.dst, copy.bufferCopy);
      }
      for (const auto& copy : mStagedImageCopies) {
        const vk::ImageSubresourceLayers& layers = copy.imageCopy.imageSubresource;
        vk::ImageSubresourceRange range{layers.aspectMask, layers.mipLevel, 1, layers.baseArrayLayer,
                                        layers.layerCount};
        vw::Image::transitionLayout(cmdBuffer, copy.dst, copy.preLayout, vk::ImageLayout::eTransferDstOptimal, range);
        cmdBuffer.copyBufferToImage(mHandle, copy.dst, vk::ImageLayout::eTransferDstOptimal, copy.imageCopy);
        vw::Image::transitionLayout(cmdBuffer, copy.dst, vk::ImageLayout::eTransferDstOptimal, copy.postLayout, range);
      }
    });
    fence->wait();
    mStagedBufferCopies.clear();
    mStagedImageCopies.clear();
    mUsedBytes = 0;
  }

 private:
  template <typename T>
  vk::DeviceSize copyToMappedEnd(const T& src) {
    copyToMapped(src, mUsedBytes);
    vk::DeviceSize srcOffset = mUsedBytes;
    mUsedBytes += vw::byteSize(src);
    return srcOffset;
  }
  struct StagedBufferCopy {
    vk::Buffer dst;
    vk::BufferCopy bufferCopy;
  };
  struct StagedImageCopy {
    vk::Image dst;
    vk::ImageLayout preLayout;
    vk::ImageLayout postLayout;
    vk::BufferImageCopy imageCopy;
  };
  std::vector<StagedBufferCopy> mStagedBufferCopies;
  std::vector<StagedImageCopy> mStagedImageCopies;
  vk::DeviceSize mUsedBytes = 0;
  vw::Queue& mTransferQueue;
};


class SimpleMemoryAllocator : public MemoryAllocator {
 public:
  SimpleMemoryAllocator(vk::PhysicalDevice physicalDevice, vk::Device device, vk::DeviceSize pageSize = 1024 * 1024);
  MemoryAllocation allocate(const vk::MemoryRequirements& requirements, MemoryPreference preference) override;
  ~SimpleMemoryAllocator();
 private:
  MemoryAllocation findRegion(uint32_t memoryType, vk::DeviceSize size, vk::DeviceSize alignment);
  void freeRegion(uint32_t memoryType, size_t blockIndex, const MemoryRegion& region) noexcept;
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
    std::byte* mappedPtr = nullptr;
  };
  std::vector<std::vector<MemoryBlock>> mPools;
  vk::DeviceSize mPageSize;
  uint32_t mDeviceLocalMask = 0, mHostVisibleCoherentMask = 0;
  std::vector<std::mutex> mPoolMutexes;  // synchronize mPools access
};

};  // namespace vw