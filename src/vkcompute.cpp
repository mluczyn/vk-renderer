#include "..\inc\vkcompute.hpp"

vw::ComputePipeline::ComputePipeline(vk::PipelineLayout layout, vk::ShaderModule computeShader) {
  vk::ComputePipelineCreateInfo createInfo{{}, vk::PipelineShaderStageCreateInfo{{}, vk::ShaderStageFlagBits::eCompute, computeShader, "main"}, layout};
  mHandle = vw::g::device.createComputePipeline(VK_NULL_HANDLE, createInfo).value;
}
