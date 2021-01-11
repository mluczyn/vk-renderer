#include "..\inc\vkrender.hpp"

vw::RenderPass::RenderPass(vk::Device device, ArrayProxy<AttachmentInfo> attachments, ArrayProxy<vk::SubpassDependency> dependencies) : ContainerType{device} {
    std::vector<vk::AttachmentReference> colorReferences, resolveReferences;
    std::optional<vk::AttachmentReference> depthReference;
    std::vector<vk::AttachmentDescription> descriptions;
    descriptions.reserve(attachments.size());
    
    for (uint32_t i = 0; i < attachments.size(); ++i) {
        const auto& attachment = attachments[i];
        descriptions.push_back(attachment.desc);
        if(attachment.use == vw::AttachmentUse::Color)
            colorReferences.emplace_back(i, vk::ImageLayout::eColorAttachmentOptimal);
        else if(attachment.use == vw::AttachmentUse::Resolve)
            resolveReferences.emplace_back(i, vk::ImageLayout::eColorAttachmentOptimal);
        else if(attachment.use == vw::AttachmentUse::DepthStencil)
            depthReference = {i, vk::ImageLayout::eDepthStencilAttachmentOptimal};
    }

    vk::SubpassDescription subpassDescription{
        {},
        vk::PipelineBindPoint::eGraphics,
        {}, {},
        vw::size32(colorReferences),
        colorReferences.data(),
        resolveReferences.data(),
        vw::optPtr(depthReference)
    };

    mHandle = mDeviceHandle.createRenderPass({
        {},
        vw::size32(descriptions),
        descriptions.data(),
        1,
        &subpassDescription,
        dependencies.size(),
        dependencies.data()
    });
}

vw::GraphicsPipeline::GraphicsPipeline(vk::Device device, const vk::GraphicsPipelineCreateInfo& createInfo) : ContainerType{device} {
    mHandle = mDeviceHandle.createGraphicsPipeline({}, createInfo);
}

vw::PipelineLayout::PipelineLayout(vk::Device device, ArrayProxy<vk::DescriptorSetLayout> setLayouts, ArrayProxy<vk::PushConstantRange> pushConstantRanges) 
: ContainerType{device} {
    mHandle = mDeviceHandle.createPipelineLayout({
        {},
        setLayouts.size(),
        setLayouts.data(),
        pushConstantRanges.size(),
        pushConstantRanges.data()
    });
}

vw::Framebuffer::Framebuffer(vk::Device device, vk::RenderPass renderPass, ArrayProxy<vk::ImageView> attachments, vk::Extent2D extent, uint32_t layers)
: ContainerType{device} {
    mHandle = mDeviceHandle.createFramebuffer({
        {},
        renderPass,
        attachments.size(),
        attachments.data(),
        extent.width,
        extent.height,
        layers
    });
}
