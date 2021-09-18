#pragma once
#include <filesystem>
#include <string>
#include "vkutils.hpp"
#include "vulkan/vulkan.hpp"

namespace vw {
class Shader {
 public:
  static vw::Shader fromFile(vk::Device& device, vk::ShaderStageFlagBits stage, const std::filesystem::path& path);
  static vw::Shader fromGlsl(vk::Device& device, vk::ShaderStageFlagBits stage, const std::string& text);
  Shader(vk::Device& device, vk::ShaderStageFlagBits stage, vw::ArrayProxy<uint32_t> binary);
  ~Shader();
  inline vk::ShaderModule getModule() {
    return mModule;
  }

 private:
  vk::Device mDeviceHandle;
  vk::ShaderModule mModule;
};
}  // namespace vw