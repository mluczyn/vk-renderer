#include "vkshader.hpp"
#include <cstddef>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include "SPIRV/GlslangToSpv.h"
#include "StandAlone/ResourceLimits.h"

static const std::map<vk::ShaderStageFlagBits, EShLanguage> MAP_SHADER_STAGE_TO_ESHLANG = {{vk::ShaderStageFlagBits::eVertex, EShLangVertex},
                                                                                           {vk::ShaderStageFlagBits::eFragment, EShLangFragment}};

vw::Shader::Shader(vk::Device& device, vk::ShaderStageFlagBits stage, vw::ArrayProxy<uint32_t> binary) : mDeviceHandle{device} {
  mModule = mDeviceHandle.createShaderModule(vk::ShaderModuleCreateInfo{vk::ShaderModuleCreateFlags{}, binary.byteSize(), binary.data()});
}

vw::Shader vw::Shader::fromGlsl(vk::Device& device, vk::ShaderStageFlagBits stage, const std::string& text) {
  glslang::InitializeProcess();

  auto lang = MAP_SHADER_STAGE_TO_ESHLANG.at(stage);
  glslang::TShader shader{lang};
  auto shaderTextPtr = reinterpret_cast<const char*>(text.data());
  auto shaderTextSize = static_cast<const int>(text.size());
  shader.setStringsWithLengths(&shaderTextPtr, &shaderTextSize, 1);
  auto messageFlags = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
  if (!shader.parse(&glslang::DefaultTBuiltInResource, 100, false, messageFlags))
    throw std::runtime_error("Shader compilation failed\n" + std::string{shader.getInfoLog()} + std::string{shader.getInfoDebugLog()});

  glslang::TProgram program;
  program.addShader(&shader);
  if (!program.link(messageFlags))
    throw std::runtime_error("Shader linking error\n" + std::string{shader.getInfoLog()} + std::string{shader.getInfoDebugLog()});

  std::vector<unsigned int> shaderSpirv;
  glslang::GlslangToSpv(*program.getIntermediate(lang), shaderSpirv);

  glslang::FinalizeProcess();

  return vw::Shader{device, stage, shaderSpirv};
}

vw::Shader vw::Shader::fromFile(vk::Device& device, vk::ShaderStageFlagBits stage, const std::filesystem::path& path) {
  if (!std::filesystem::exists(path))
    throw std::runtime_error("Shader file " + path.string() + " does not exist!");
  if (!std::filesystem::is_regular_file(path))
    throw std::runtime_error("Shader file " + path.string() + " is not a regular file!");

  auto fSize = std::filesystem::file_size(path);
  if (fSize % sizeof(uint32_t) != 0)
    throw std::runtime_error("Shader binary " + path.string() + " size is not a multiple of 4 ");

  uint32_t wordCount = fSize / sizeof(uint32_t);
  auto binary = std::make_unique<uint32_t[]>(wordCount);
  {
    std::ifstream shaderFile{path, std::ios::binary};
    if (!shaderFile.read(reinterpret_cast<char*>(binary.get()), fSize))
      throw std::runtime_error("Error reading shader file " + path.string());
  }

  return vw::Shader{device, stage, vw::ArrayProxy<uint32_t>{binary.get(), wordCount}};
}

vw::Shader::~Shader() {
  mDeviceHandle.destroyShaderModule(mModule);
}
