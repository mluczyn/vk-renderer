#include "..\inc\vkrender.hpp"

vw::RenderPass::RenderPass(ArrayProxy<AttachmentInfo> attachments, ArrayProxy<vk::SubpassDependency> dependencies) {
  std::vector<vk::AttachmentReference> colorReferences, resolveReferences;
  std::optional<vk::AttachmentReference> depthReference;
  std::vector<vk::AttachmentDescription> descriptions;
  descriptions.reserve(attachments.size());

  for (uint32_t i = 0; i < attachments.size(); ++i) {
    const auto& attachment = attachments[i];
    descriptions.push_back(attachment.desc);
    if (attachment.use == vw::AttachmentUse::Color)
      colorReferences.emplace_back(i, vk::ImageLayout::eColorAttachmentOptimal);
    else if (attachment.use == vw::AttachmentUse::Resolve)
      resolveReferences.emplace_back(i, vk::ImageLayout::eColorAttachmentOptimal);
    else if (attachment.use == vw::AttachmentUse::DepthStencil)
      depthReference = {i, vk::ImageLayout::eDepthStencilAttachmentOptimal};
  }

  vk::SubpassDescription subpassDescription{
      {}, vk::PipelineBindPoint::eGraphics, {}, {}, vw::size32(colorReferences), colorReferences.data(), resolveReferences.data(), vw::optPtr(depthReference)};

  mHandle =
      vw::g::device.createRenderPass({{}, vw::size32(descriptions), descriptions.data(), 1, &subpassDescription, dependencies.size(), dependencies.data()});
}

vw::GraphicsPipeline::GraphicsPipeline(const vk::GraphicsPipelineCreateInfo& createInfo) {
  mHandle = vw::g::device.createGraphicsPipeline({}, createInfo).value;
}

vw::Framebuffer::Framebuffer(vk::RenderPass renderPass, ArrayProxy<vk::ImageView> attachments, vk::Extent2D extent, uint32_t layers) {
  mHandle = vw::g::device.createFramebuffer({{}, renderPass, attachments.size(), attachments.data(), extent.width, extent.height, layers});
}
