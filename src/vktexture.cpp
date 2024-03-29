#include "vktexture.hpp"
#include <fstream>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

vw::GenericImageFile::GenericImageFile(const std::filesystem::path& path, int requiredCompCount) {
  if (!std::filesystem::exists(path))
    throw std::runtime_error("Texture file " + path.string() + " does not exist!");
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("Texture file " + path.string() + " is not a regular file!");

  int width, height, comp;
  mDataStart = reinterpret_cast<std::byte*>(stbi_load(path.string().c_str(), &width, &height, &comp, requiredCompCount));
  mSize = static_cast<uint64_t>(width) * static_cast<uint64_t>(height) * static_cast<uint64_t>(requiredCompCount);
  mExtent.width = width;
  mExtent.height = height;
  mExtent.depth = 1;
}

vw::GenericImageFile::~GenericImageFile() {
  stbi_image_free(mDataStart);
}

vw::Sampler::Sampler(vk::Filter filter, vk::SamplerAddressMode addressMode, float maxAnisotropy) {
  vk::SamplerCreateInfo createInfo;
  createInfo.magFilter = filter;
  createInfo.minFilter = filter;
  createInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
  createInfo.addressModeU = addressMode;
  createInfo.addressModeV = addressMode;
  createInfo.addressModeW = addressMode;
  createInfo.anisotropyEnable = (maxAnisotropy > 0.0f);
  createInfo.maxAnisotropy = maxAnisotropy;
  createInfo.compareEnable = VK_FALSE;
  createInfo.compareOp = vk::CompareOp::eNever;
  createInfo.borderColor = vk::BorderColor::eFloatOpaqueBlack;

  mHandle = vw::g::device.createSampler(createInfo);
}
