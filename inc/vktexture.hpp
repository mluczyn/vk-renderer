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
  vk::Extent3D getExtent() const override {
    return mExtent;
  }
  vk::Format getFormat() const override {
    return vk::Format::eR8G8B8A8Unorm;
  }
  vk::DeviceSize dataSize() const override {
    return mSize;
  }
  void loadData(std::byte* dest) const override {
    std::copy(mDataStart, mDataStart + mSize, dest);
  }
  std::byte* begin() const {
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

template <typename T>
class DefaultValueFile : public ImageFile {
 public:
  DefaultValueFile(const T& defaultValue, vk::Format format) : mDefaultValue{defaultValue}, mFormat{format} {}
  vk::Extent3D getExtent() const override {
    return {1, 1, 1};
  }
  vk::Format getFormat() const override {
    return mFormat;
  }
  vk::DeviceSize dataSize() const override {
    return sizeof(T);
  }
  void loadData(std::byte* dest) const override {
    *reinterpret_cast<T*>(dest) = mDefaultValue;
  }

 private:
  T mDefaultValue;
  vk::Format mFormat;
};

class Sampler : public vw::HandleContainerUnique<vk::Sampler> {
 public:
  Sampler(vk::Filter filter = vk::Filter::eLinear, vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat, float maxAnisotropy = 4.0f);
};

}  // namespace vw