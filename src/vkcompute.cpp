#include "..\inc\vkcompute.hpp"

vw::ComputePipeline::ComputePipeline(vk::Device device, vk::PipelineLayout layout, vk::ShaderModule computeShader) : ContainerType{device} {
  mHandle = mDeviceHandle.createComputePipeline(VK_NULL_HANDLE, {
    {},
    vk::PipelineShaderStageCreateInfo{
      {},
      vk::ShaderStageFlagBits::eCompute,
      computeShader,
      "main"
    },
    layout
  }).value;
}
