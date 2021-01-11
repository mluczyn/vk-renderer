#pragma once
#include "vkutils.hpp"
#include "vkcore.hpp"

namespace vw {
    enum AttachmentUse {
        Color,
        DepthStencil,
        Resolve
    };

    struct AttachmentInfo {
        AttachmentUse use;
        vk::AttachmentDescription desc;
    };

    class RenderPass : public vw::HandleContainerUnique<vk::RenderPass> {
    public:
        RenderPass(vk::Device device, ArrayProxy<AttachmentInfo> attachments, ArrayProxy<vk::SubpassDependency> dependencies);
        static inline constexpr AttachmentInfo colorAtt(vk::Format format, bool store = true,
                                                        vk::ImageLayout finalLayout = vk::ImageLayout::eColorAttachmentOptimal,
                                                        vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1) {
            return {
                AttachmentUse::Color,
                vk::AttachmentDescription{
                    {},
                    format,
                    sampleCount,
                    vk::AttachmentLoadOp::eClear,
                    store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
                    {}, {}, {},
                    finalLayout
                }
            };
        }
        static inline constexpr AttachmentInfo resolvePresentAtt(vk::Format format) {
            return {
                AttachmentUse::Resolve,
                vk::AttachmentDescription{
                    {},
                    format,
                    vk::SampleCountFlagBits::e1,
                    vk::AttachmentLoadOp::eDontCare,
                    vk::AttachmentStoreOp::eStore,
                    {}, {}, {},
                    vk::ImageLayout::ePresentSrcKHR
                }
            };
        }
        static inline constexpr AttachmentInfo depthAtt(vk::Format format, bool store = false,
                                                           vk::SampleCountFlagBits sampleCount = vk::SampleCountFlagBits::e1) {
            return {
                AttachmentUse::DepthStencil,
                vk::AttachmentDescription{
                    {},
                    format,
                    sampleCount,
                    vk::AttachmentLoadOp::eClear,
                    store ? vk::AttachmentStoreOp::eStore : vk::AttachmentStoreOp::eDontCare,
                    {}, {}, {},
                    vk::ImageLayout::eDepthStencilAttachmentOptimal
                }
            };
        }
        static constexpr vk::SubpassDependency externalColorOutputDependency{
            VK_SUBPASS_EXTERNAL, 0,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::AccessFlagBits::eColorAttachmentWrite,
            vk::AccessFlagBits::eColorAttachmentWrite,
            vk::DependencyFlagBits::eByRegion
        };
        static constexpr vk::SubpassDependency externalDepthStencilIODependency{
            VK_SUBPASS_EXTERNAL, 0,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            vk::PipelineStageFlagBits::eEarlyFragmentTests,
            vk::AccessFlagBits::eDepthStencilAttachmentRead | vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            vk::AccessFlagBits::eDepthStencilAttachmentWrite,
            vk::DependencyFlagBits::eByRegion
        };
    };

    constexpr vk::ColorComponentFlags AllColorComponents = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                                                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

    class GraphicsPipelineBuilder {
    public:
        GraphicsPipelineBuilder(vk::PipelineLayout layout, vk::RenderPass renderpass, uint32_t subpassIndex = 0)
        : mLayoutHandle{layout}, mRenderPassHandle{renderpass}, mSubpassIndex{subpassIndex} {}
        inline void addShaderStage(vk::ShaderStageFlagBits stage, vk::ShaderModule module) {
            mShaderStages.push_back(vk::PipelineShaderStageCreateInfo{{}, stage, module, "main"});
        }
        inline void setVertexInputState(ArrayProxy<vk::VertexInputBindingDescription> inputBindingDescriptions,
                                        ArrayProxy<vk::VertexInputAttributeDescription> inputAttributeDescriptions) {
            mInputBindingDescriptions.assign(inputBindingDescriptions.begin(), inputBindingDescriptions.end());
            mInputAttributeDescriptions.assign(inputAttributeDescriptions.begin(), inputAttributeDescriptions.end());

            mVertexInputState.vertexBindingDescriptionCount = vw::size32(mInputBindingDescriptions);
            mVertexInputState.pVertexBindingDescriptions = mInputBindingDescriptions.data();
            mVertexInputState.vertexAttributeDescriptionCount = vw::size32(mInputAttributeDescriptions);
            mVertexInputState.pVertexAttributeDescriptions = mInputAttributeDescriptions.data();
        }
        inline void setInputAssemblyState(vk::PrimitiveTopology topology, bool primitiveRestartEnable = false) {
            mInputAssemblyState = vk::PipelineInputAssemblyStateCreateInfo{};
            mInputAssemblyState->topology = topology;
            mInputAssemblyState->primitiveRestartEnable = primitiveRestartEnable;
        }
        inline void setViewportState(ArrayProxy<vk::Viewport> viewports, ArrayProxy<vk::Rect2D> scissors) {
            mViewports.assign(viewports.begin(), viewports.end());
            mScissors.assign(scissors.begin(), scissors.end());

            mViewportState = vk::PipelineViewportStateCreateInfo{};
            mViewportState->viewportCount = vw::size32(mViewports);
            mViewportState->pViewports = mViewports.data();
            mViewportState->scissorCount = vw::size32(mScissors);
            mViewportState->pScissors = mScissors.data();
        }
        inline void setRasterizationState(vk::PolygonMode polygonMode, vk::CullModeFlags cullMode, 
                                          vk::FrontFace frontFace = vk::FrontFace::eClockwise) {
            mRasterizationState = vk::PipelineRasterizationStateCreateInfo{};
            mRasterizationState->depthClampEnable = false;
            mRasterizationState->rasterizerDiscardEnable = false;
            mRasterizationState->polygonMode = polygonMode;
            mRasterizationState->cullMode = cullMode;
            mRasterizationState->frontFace = frontFace;
            mRasterizationState->lineWidth = 1.0f;
        }
        inline void setMultisampleState(vk::SampleCountFlagBits sampleCount) {
            mMultisampleState.rasterizationSamples = sampleCount;
        }
        void setBlendState(ArrayProxy<vk::PipelineColorBlendAttachmentState> attachments,
                           bool logicOpEnable = false, vk::LogicOp logicOp = {}, 
                           const std::array<float, 4>& blendConstants = {}) {
            mBlendAttachments.assign(attachments.begin(), attachments.end());

            mBlendState.logicOpEnable = static_cast<vk::Bool32>(logicOpEnable);
            mBlendState.logicOp = logicOp;
            mBlendState.attachmentCount = vw::size32(mBlendAttachments);
            mBlendState.pAttachments = mBlendAttachments.data();
            mBlendState.blendConstants[0] = blendConstants[0];
            mBlendState.blendConstants[1] = blendConstants[1];
            mBlendState.blendConstants[2] = blendConstants[2];
            mBlendState.blendConstants[3] = blendConstants[3];
        }
        inline void setDynamicState(ArrayProxy<vk::DynamicState> dynamicStates) {
            mDynamicStates.assign(dynamicStates.begin(), dynamicStates.end());
            mDynamicState.dynamicStateCount = vw::size32(mDynamicStates);
            mDynamicState.pDynamicStates = mDynamicStates.data();
        }
        inline void setLayout(vk::PipelineLayout layout) {
            mLayoutHandle = layout;
        }
        inline void setRenderpass(vk::RenderPass renderPass, uint32_t subpass = 0) {
            mRenderPassHandle = renderPass;
            mSubpassIndex = subpass;
        }
        inline void setDepthTestState(bool enableDepthTest, bool enableDepthWrite, vk::CompareOp depthCompareOp = vk::CompareOp::eLess) {
            if(!mDepthStencilState)
                mDepthStencilState = vk::PipelineDepthStencilStateCreateInfo{};

            mDepthStencilState->depthTestEnable = enableDepthTest;
            mDepthStencilState->depthWriteEnable = enableDepthWrite;
            mDepthStencilState->depthCompareOp = depthCompareOp;
        }
        inline vk::GraphicsPipelineCreateInfo getCreateInfo() {
            return vk::GraphicsPipelineCreateInfo{
                {},
                vw::size32(mShaderStages),
                mShaderStages.data(),
                &mVertexInputState,
                vw::optPtr(mInputAssemblyState),
                {},
                vw::optPtr(mViewportState),
                vw::optPtr(mRasterizationState),
                &mMultisampleState,
                vw::optPtr(mDepthStencilState),
                &mBlendState,
                mDynamicStates.size() ? &mDynamicState : nullptr,
                mLayoutHandle,
                mRenderPassHandle,
                mSubpassIndex
            };
        }
        static constexpr vk::PipelineColorBlendAttachmentState noBlendAttachment{
                {},
                vk::BlendFactor::eOne,
                vk::BlendFactor::eZero,
                vk::BlendOp::eAdd,
                vk::BlendFactor::eOne,
                vk::BlendFactor::eZero,
                vk::BlendOp::eAdd,
                vw::AllColorComponents
        };
    private:
        std::vector<vk::PipelineShaderStageCreateInfo> mShaderStages;

        std::vector<vk::VertexInputBindingDescription> mInputBindingDescriptions;
        std::vector<vk::VertexInputAttributeDescription> mInputAttributeDescriptions;
        vk::PipelineVertexInputStateCreateInfo mVertexInputState;

        std::optional<vk::PipelineInputAssemblyStateCreateInfo> mInputAssemblyState;

        std::vector<vk::Viewport> mViewports;
        std::vector<vk::Rect2D> mScissors;
        std::optional<vk::PipelineViewportStateCreateInfo> mViewportState;
        
        std::optional<vk::PipelineRasterizationStateCreateInfo> mRasterizationState;

        vk::PipelineMultisampleStateCreateInfo mMultisampleState;

        std::vector<vk::PipelineColorBlendAttachmentState> mBlendAttachments;
        vk::PipelineColorBlendStateCreateInfo mBlendState;

        std::optional<vk::PipelineDepthStencilStateCreateInfo> mDepthStencilState;

        std::vector<vk::DynamicState> mDynamicStates;
        vk::PipelineDynamicStateCreateInfo mDynamicState;

        vk::PipelineLayout mLayoutHandle;
        vk::RenderPass mRenderPassHandle;
        uint32_t mSubpassIndex;
    };

    class GraphicsPipeline : public vw::HandleContainerUnique<vk::Pipeline> {
    public:
        GraphicsPipeline(vk::Device device, const vk::GraphicsPipelineCreateInfo& createInfo);
    };

    class PipelineLayout : public vw::HandleContainerUnique<vk::PipelineLayout> {
    public:
        PipelineLayout(vk::Device device, ArrayProxy<vk::DescriptorSetLayout> setLayouts = {}, ArrayProxy<vk::PushConstantRange> pushConstantRanges = {});
    };

    class Framebuffer : public vw::HandleContainerUnique<vk::Framebuffer> {
    public:
        Framebuffer(vk::Device device, vk::RenderPass renderPass, ArrayProxy<vk::ImageView> attachments, vk::Extent2D extent, uint32_t layers = 1);
    };
}