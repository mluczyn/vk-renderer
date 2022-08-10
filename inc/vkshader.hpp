#pragma once
#include <filesystem>
#include <string>
#include "vkutils.hpp"
#include "vulkan/vulkan.hpp"

namespace vw {

struct ShaderIOBinding {
  ShaderIOBinding(vk::DescriptorType type, uint32_t count = 1, uint32_t setIndex = 0, bool isVariable = false)
      : type{type}, count{count}, setIndex{setIndex}, isVariable{isVariable} {}
  vk::DescriptorType type;
  uint32_t count;
  uint32_t setIndex;
  bool isVariable;
};

std::vector<uint32_t> loadShader(std::filesystem::path path);

class Shader : public vw::HandleContainerUnique<vk::ShaderModule> {
 public:
  Shader(vk::ShaderStageFlagBits stage, vw::ArrayProxy<uint32_t> binary, vw::ArrayProxy<ShaderIOBinding> ioBindings, uint32_t pushConstantSize = 0);
  const std::vector<ShaderIOBinding>& getIOBindings() const {
    return mIOBindings;
  }
  uint32_t getPushConstantSize() const {
    return mPushConstantSize;
  }
  vk::ShaderStageFlagBits getStage() const {
    return mStage;
  }

 private:
  std::vector<ShaderIOBinding> mIOBindings;
  uint32_t mPushConstantSize;
  vk::ShaderStageFlagBits mStage;
};
}  // namespace vw