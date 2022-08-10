#pragma once
#include <vk_mem_alloc.h>
#include <functional>
#include <future>
#include <vulkan/vulkan.hpp>
#include "vkcore.hpp"
#include "vkutils.hpp"

namespace vw {

constexpr vk::ImageSubresourceLayers kDefaultImageLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1};

namespace BufferUse {
constexpr vk::BufferUsageFlags kStagingBuffer = vk::BufferUsageFlagBits::eTransferSrc;
constexpr vk::BufferUsageFlags kUniformBuffer = vk::BufferUsageFlagBits::eUniformBuffer;
constexpr vk::BufferUsageFlags kStorageBuffer = vk::BufferUsageFlagBits::eStorageBuffer;
constexpr vk::BufferUsageFlags kVertexBuffer = vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst;
constexpr vk::BufferUsageFlags kIndexBuffer = vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst;
}  // namespace BufferUse

namespace ImageUse {
constexpr vk::ImageUsageFlags kTexture = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst;
}

class MemoryAllocator {
 public:
  MemoryAllocator();
  ~MemoryAllocator();
  VmaAllocator getHandle() {
    return mHandle;
  }

 private:
  VmaAllocator mHandle = VK_NULL_HANDLE;
};

class Buffer : public vw::HandleContainerUnique<vk::Buffer> {
 public:
  Buffer(MemoryAllocator& allocator,
         vw::ArrayProxy<vk::DeviceSize> segmentSizes,
         vk::BufferUsageFlags usage,
         VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY);
  ~Buffer();
  Buffer(const Buffer& other) = delete;
  Buffer& operator=(const Buffer& other) = delete;
  template <typename T>
  void copyToMapped(const T& src, size_t segmentIdx = 0, vk::DeviceSize segmentOffset = 0) {
    assert(mMappedPtr != nullptr);
    assert(segmentIdx < mSegmentBase.size());
    vk::DeviceSize srcSize = vw::byteSize(src);
    assert(segmentOffset < mSegmentSizes[segmentIdx]);
    auto* dest = reinterpret_cast<typename T::value_type*>(mMappedPtr + mSegmentBase[segmentIdx] + segmentOffset);
    std::copy(std::begin(src), std::end(src), dest);
  }
  vk::DescriptorBufferInfo getSegmentDesc(size_t idx) const {
    return {mHandle, mSegmentBase[idx], mSegmentSizes[idx]};
  }

 protected:
  vw::FixedVec<vk::DeviceSize> mSegmentBase;
  vw::FixedVec<vk::DeviceSize> mSegmentSizes;
  vk::DeviceSize mAlignment;
  VmaAllocator mAllocator;
  VmaAllocation mAllocation;
  VmaAllocationInfo mAllocationInfo;
  std::byte* mMappedPtr;
};

class ImageView : public vw::HandleContainerUnique<vk::ImageView> {
 public:
  ImageView(vk::ImageView handle);
};

class Image : public vw::HandleContainerUnique<vk::Image> {
 public:
  static constexpr vk::ImageSubresourceRange kDefaultSubResourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
  Image(MemoryAllocator& allocator,
        vk::Format format,
        vk::Extent3D extent,
        vk::ImageUsageFlags usage,
        vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1,
        vk::ImageType type = vk::ImageType::e2D,
        uint32_t mipLevels = 1,
        uint32_t arrayLayers = 1,
        VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY);
  ~Image();
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
  VmaAllocator mAllocator;
  VmaAllocation mAllocation;
  VmaAllocationInfo mAllocationInfo;
};

class StagingBuffer : public vw::Buffer {
 public:
  StagingBuffer(MemoryAllocator& allocator, vk::DeviceSize size, vw::Queue& transferQueue)
      : vw::Buffer{allocator, size, vw::BufferUse::kStagingBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU}, mTransferQueue{transferQueue} {}
  template <typename T>
  void queueBufferCopy(const T& src, vk::Buffer dst, vk::DeviceSize dstOffset = 0) {
    vk::DeviceSize dataSize = vw::byteSize(src);
    if (dataSize > mSegmentSizes[0])
      throw std::runtime_error("Data size exceeds staging buffer size");
    if (dataSize > remainingSpace())
      flush();
    vk::DeviceSize srcOffset = copyToMappedEnd(src);
    mStagedBufferCopies.push_back({dst, vk::BufferCopy{srcOffset, dstOffset, vw::byteSize(src)}});
  }
  template <typename T>
  void queueBufferCopies(const T& srcs, vk::Buffer dst, vk::DeviceSize baseDstOffset = 0) {
    vk::DeviceSize totalSize = 0;
    for (const auto& src : srcs)
      totalSize += vw::byteSize(src);

    if (totalSize > mSegmentSizes[0]) {
      vk::DeviceSize dstOffset = baseDstOffset;
      for (const auto& src : srcs) {
        queueBufferCopy(src, dst, dstOffset);
        dstOffset += vw::byteSize(src);
      }
    } else {
      if (totalSize > remainingSpace())
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
    if (imageDataSize > mSegmentSizes[0])
      throw std::runtime_error("Image data size exceeds staging buffer size");
    if (imageDataSize > remainingSpace())
      flush();
    vk::DeviceSize srcOffset = mUsedBytes;
    imageFile.loadData(mMappedPtr + srcOffset);
    mUsedBytes += imageDataSize;

    vk::BufferImageCopy copyInfo;
    copyInfo.bufferOffset = srcOffset;
    copyInfo.imageExtent = imageFile.getExtent();
    copyInfo.imageOffset = destOffset;
    copyInfo.imageSubresource = layers;
    mStagedImageCopies.push_back({dst, preLayout, postLayout, copyInfo});
  }
  vk::DeviceSize remainingSpace() const {
    return mSegmentSizes[0] - mUsedBytes;
  }
  void flush() {
    auto fence = mTransferQueue.oneTimeRecordSubmit([&](vw::CommandBuffer& cmdBuffer) {
      for (const auto& copy : mStagedBufferCopies) {
        cmdBuffer.copyBuffer(mHandle, copy.dst, copy.bufferCopy);
      }
      for (const auto& copy : mStagedImageCopies) {
        const vk::ImageSubresourceLayers& layers = copy.imageCopy.imageSubresource;
        vk::ImageSubresourceRange range{layers.aspectMask, layers.mipLevel, 1, layers.baseArrayLayer, layers.layerCount};
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
    copyToMapped(src, 0, mUsedBytes);
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

};  // namespace vw