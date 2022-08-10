#pragma once
#include "vkutils.hpp"

namespace vw {

class ComputePipeline : public vw::HandleContainerUnique<vk::Pipeline> {
 public:
  ComputePipeline(vk::PipelineLayout layout, vk::ShaderModule computeShader);
};

};  // namespace vw