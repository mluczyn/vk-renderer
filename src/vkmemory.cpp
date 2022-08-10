#define VMA_IMPLEMENTATION
#include "..\inc\vkmemory.hpp"
#include <exception>
#include "vulkan/vulkan.hpp"

int32_t findProperties(const vk::PhysicalDeviceMemoryProperties& memoryProperties,
                       uint32_t memoryTypeBitsRequirement,
                       vk::MemoryPropertyFlags requiredProperties) {
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

vw::Buffer::Buffer(MemoryAllocator& allocator, vw::ArrayProxy<vk::DeviceSize> segmentSizes, vk::BufferUsageFlags usage, VmaMemoryUsage memoryUsage)
    : mSegmentSizes{segmentSizes.size()}, mSegmentBase{segmentSizes.size()}, mAllocator{allocator.getHandle()} {
  constexpr vk::DeviceSize cAlign = 256;
  vk::DeviceSize nextSegmentBase = 0;
  for (const auto& v : segmentSizes) {
    mSegmentBase.emplace_back(nextSegmentBase);
    mSegmentSizes.emplace_back(v);
    nextSegmentBase += v;
    alignTo(nextSegmentBase, cAlign);
  }

  vk::BufferCreateInfo bufferCreateInfo{{}, nextSegmentBase, usage};

  VmaAllocationCreateInfo allocationCreateInfo{};
  allocationCreateInfo.usage = memoryUsage;
  if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU)
    allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VkBuffer buffer;
  VkResult r = vmaCreateBuffer(mAllocator, &static_cast<VkBufferCreateInfo>(bufferCreateInfo), &allocationCreateInfo, &buffer, &mAllocation, &mAllocationInfo);
  if (r != VK_SUCCESS)
    throw std::runtime_error("Failed to create buffer");

  mHandle = buffer;
  mMappedPtr = reinterpret_cast<std::byte*>(mAllocationInfo.pMappedData);
}

vw::Buffer::~Buffer() {
  if (mHandle)
    vmaDestroyBuffer(mAllocator, mHandle, mAllocation);
}

const static std::unordered_map<vk::ImageLayout, vk::AccessFlags> MAP_LAYOUT_TO_ACCESS_FLAGS{
    {vk::ImageLayout::eUndefined, {}},
    {vk::ImageLayout::eTransferDstOptimal, vk::AccessFlagBits::eTransferWrite},
    {vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eShaderRead},
    {vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead},
    {vk::ImageLayout::eGeneral, vk::AccessFlagBits::eShaderWrite},
    {vk::ImageLayout::ePresentSrcKHR, {}}};

const static std::unordered_map<vk::ImageLayout, vk::PipelineStageFlags> MAP_LAYOUT_TO_STAGE{
    {vk::ImageLayout::eUndefined, vk::PipelineStageFlagBits::eTopOfPipe},
    {vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits::eTransfer},
    {vk::ImageLayout::eShaderReadOnlyOptimal, vk::PipelineStageFlagBits::eFragmentShader},
    {vk::ImageLayout::eDepthStencilAttachmentOptimal, vk::PipelineStageFlagBits::eEarlyFragmentTests},
    {vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits::eComputeShader},
    {vk::ImageLayout::ePresentSrcKHR, vk::PipelineStageFlagBits::eBottomOfPipe}};

vw::Image::Image(MemoryAllocator& allocator,
                 vk::Format format,
                 vk::Extent3D extent,
                 vk::ImageUsageFlags usage,
                 vk::SampleCountFlagBits samples,
                 vk::ImageType type,
                 uint32_t mipLevels,
                 uint32_t arrayLayers,
                 VmaMemoryUsage memoryUsage)
    : mExtent{extent}, mFormat{format}, mAllocator{allocator.getHandle()} {
  vk::ImageCreateInfo createInfo;
  createInfo.imageType = type;
  createInfo.format = format;
  createInfo.extent = extent;
  createInfo.mipLevels = mipLevels;
  createInfo.arrayLayers = arrayLayers;
  createInfo.samples = samples;
  createInfo.tiling = vk::ImageTiling::eOptimal;
  createInfo.usage = usage;
  createInfo.initialLayout = vk::ImageLayout::eUndefined;

  VmaAllocationCreateInfo allocationCreateInfo{};
  allocationCreateInfo.usage = memoryUsage;
  if (memoryUsage == VMA_MEMORY_USAGE_CPU_TO_GPU)
    allocationCreateInfo.flags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;

  VkImage image;
  VkResult r = vmaCreateImage(mAllocator, &static_cast<VkImageCreateInfo>(createInfo), &allocationCreateInfo, &image, &mAllocation, &mAllocationInfo);
  if (r != VK_SUCCESS)
    throw std::runtime_error("Failed to create image");

  mHandle = image;
}

vw::Image::~Image() {
  if (mHandle)
    vmaDestroyImage(mAllocator, mHandle, mAllocation);
}

void vw::Image::transitionLayout(vk::CommandBuffer cmdBuffer,
                                 vk::Image image,
                                 vk::ImageLayout oldLayout,
                                 vk::ImageLayout newLayout,
                                 vk::ImageSubresourceRange range) {
  vk::ImageMemoryBarrier barrier;
  vk::PipelineStageFlags srcStageMask, dstStageMask;

  try {
    barrier.srcAccessMask = MAP_LAYOUT_TO_ACCESS_FLAGS.at(oldLayout);
    barrier.dstAccessMask = MAP_LAYOUT_TO_ACCESS_FLAGS.at(newLayout);
    srcStageMask = MAP_LAYOUT_TO_STAGE.at(oldLayout);
    dstStageMask = MAP_LAYOUT_TO_STAGE.at(newLayout);
  } catch (std::out_of_range&) {
    throw std::runtime_error("Unsupported layout transition!");
  }

  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.image = image;
  barrier.subresourceRange = range;

  cmdBuffer.pipelineBarrier(srcStageMask, dstStageMask, {}, {}, {}, barrier);
}

void vw::Image::transitionLayout(vk::CommandBuffer cmdBuffer, vk::ImageLayout oldLayout, vk::ImageLayout newLayout, vk::ImageSubresourceRange range) const {
  transitionLayout(cmdBuffer, mHandle, oldLayout, newLayout, range);
}

void vw::Image::copyFromBuffer(vk::CommandBuffer cmdBuffer,
                               vk::Buffer src,
                               vk::DeviceSize srcOffset,
                               vk::Extent3D destExtent,
                               vk::Offset3D destOffset,
                               vk::ImageLayout initialLayout,
                               vk::ImageLayout finalLayout,
                               vk::ImageSubresourceLayers layers) const {
  transitionLayout(cmdBuffer, initialLayout, vk::ImageLayout::eTransferDstOptimal);
  vk::BufferImageCopy copyInfo;
  copyInfo.bufferOffset = srcOffset;
  copyInfo.imageExtent = destExtent;
  copyInfo.imageOffset = destOffset;
  copyInfo.imageSubresource = layers;
  cmdBuffer.copyBufferToImage(src, mHandle, vk::ImageLayout::eTransferDstOptimal, copyInfo);
  transitionLayout(cmdBuffer, vk::ImageLayout::eTransferDstOptimal, finalLayout);
}

vw::ImageView vw::Image::createView(vk::ImageViewType viewType, vk::ImageSubresourceRange range, vk::ComponentMapping components) const {
  vk::ImageViewCreateInfo createInfo;
  createInfo.image = mHandle;
  createInfo.viewType = viewType;
  createInfo.format = mFormat;
  createInfo.components = components;
  createInfo.subresourceRange = range;
  return vw::ImageView{vw::g::device.createImageView(createInfo)};
}

vw::ImageView::ImageView(vk::ImageView handle) {
  mHandle = handle;
}

vw::MemoryAllocator::MemoryAllocator() {
  VmaAllocatorCreateInfo createInfo{};
  createInfo.instance = vw::g::instance;
  createInfo.physicalDevice = vw::g::physicalDevice;
  createInfo.device = vw::g::device;
  createInfo.vulkanApiVersion = VK_API_VERSION_1_2;

  createInfo.flags |= VMA_ALLOCATOR_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;

  VkResult r = vmaCreateAllocator(&createInfo, &mHandle);
  if (r != VK_SUCCESS)
    throw std::runtime_error("Failed to create MemoryAllocator");
}

vw::MemoryAllocator::~MemoryAllocator() {
  if (mHandle)
    vmaDestroyAllocator(mHandle);
}