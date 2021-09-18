#pragma once
#include <filesystem>
#include <vulkan/vulkan.hpp>
#include "vkutils.hpp"

namespace vw {

class GenericImageFile : public ImageFile {
 public:
  using value_type = std::byte;
  GenericImageFile(const std::filesystem::path& path, int requiredCompCount = 4);
  ~GenericImageFile();
  inline vk::Extent3D getExtent() const override {
    return mExtent;
  }
  inline vk::Format getFormat() const override {
    return vk::Format::eR8G8B8A8Unorm;
  }
  inline vk::DeviceSize dataSize() const override {
    return mSize;
  }
  inline void loadData(std::byte* dest) const override {
    std::copy(mDataStart, mDataStart + mSize, dest);
  }
  inline std::byte* begin() const {
    return mDataStart;
  }
  inline std::byte* end() const {
    return mDataStart + mSize;
  }
  inline vk::DeviceSize size() const {
    return mSize;
  }
 private:
  std::byte* mDataStart = nullptr;
  vk::DeviceSize mSize;
  vk::Extent3D mExtent;
};

class Sampler : public vw::HandleContainerUnique<vk::Sampler> {
 public:
  Sampler(vk::Device device,
          vk::Filter filter = vk::Filter::eLinear,
          vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat,
          float maxAnisotropy = 16.0f);
};

}  // namespace vw