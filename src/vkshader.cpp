#include "vkshader.hpp"
#include <cstddef>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

std::vector<uint32_t> vw::loadShader(std::filesystem::path path) {
  return vw::loadBinaryFile<uint32_t>(path);
}

vw::Shader::Shader(vk::ShaderStageFlagBits stage, vw::ArrayProxy<uint32_t> binary, vw::ArrayProxy<ShaderIOBinding> ioBindings, uint32_t pushConstantSize)
    : mIOBindings{ioBindings.copyToVec()}, mPushConstantSize{pushConstantSize}, mStage{stage} {
  mHandle = vw::g::device.createShaderModule(vk::ShaderModuleCreateInfo{vk::ShaderModuleCreateFlags{}, binary.byteSize(), binary.data()});
}