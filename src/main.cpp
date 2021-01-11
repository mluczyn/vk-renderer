#include <iostream>
#include <algorithm>
#include <thread>
#include <glm/vec3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "vkpresent.hpp"
#include "vkshader.hpp"
#include "vkmemory.hpp"
#include "vkrender.hpp"
#include "vkdescriptor.hpp"
#include "vktexture.hpp"
#include "vkmodel.hpp"
#include "vkcamera.hpp"
#include "shadersource.hpp"

int main() {
    try {
        CameraInputHandler camera;

        vw::Instance instance{"App", 1, vw::getInstancePresentationExtensions()};

        vw::Extent windowExtent{1000, 800};
        vk::Rect2D windowRect{{0,0}, windowExtent};
        vw::Window<CameraInputHandler> window{instance, windowExtent, "Test"};
        window.setInputHandler(camera);

        vw::QueueWorkType mainWorkType{vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, window.getSurface()};

        vw::Device device{instance.findPhysicalDevice(mainWorkType, vw::swapchainExtension).value(), vw::swapchainExtension};
        auto queue = device.getPreferredQueue(mainWorkType);

        vw::Swapchain swapchain{device, device.getPhysicalDevice(), window.getSurface(), queue};

        vw::Shader offscreenVertShader{device, vk::ShaderStageFlagBits::eVertex, offscreenVertexShaderText};
        vw::Shader offscreenFragShader{device, vk::ShaderStageFlagBits::eFragment, offscreenFragmentShaderText};
        vw::Shader deferredVertShader{device, vk::ShaderStageFlagBits::eVertex, deferredVertexShaderText};
        vw::Shader deferredFragShader{device, vk::ShaderStageFlagBits::eFragment, deferredFragmentShaderText};

        struct LightInfo {
            glm::vec4 pos;
            glm::vec3 color;
            float radius;
        };
        std::array<LightInfo, 2> lightInfos{
            LightInfo{glm::vec4(3.0f, -3.0f, 3.0f, 1.0f), glm::vec3(1.0f), 12.0f},
            LightInfo{glm::vec4(-3.0f, -3.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 6.0f}
        };

        struct PushData {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
        } pushData;

        pushData.model = glm::scale(glm::mat4(1.0f), {0.01f, 0.01f, 0.01f});
        pushData.model = glm::rotate(pushData.model, glm::radians(90.0f), {1.0f, 0.0f, 0.0f});
        pushData.proj = glm::perspective(glm::radians(70.0f), static_cast<float>(windowExtent.width / windowExtent.height), 0.1f, 100.0f);

        vw::MemoryAllocator allocator{device.getPhysicalDevice(), device, 1024};

        vw::ModelFile modelFile{"drone/Drone.fbx"};
        if(modelFile.getMeshes().size() == 0)
            throw std::runtime_error("Invalid model file");
        auto& mesh = modelFile.getMeshes()[0];

        vw::ImageFile textureFile{"drone/Drone_diff.jpg", 4};
        vw::ImageFile normalsFile{"drone/Drone_normal.jpg", 4};
        vw::ImageFile specularFile{"drone/Drone_spec.jpg", 4};

        auto vbo = allocator.createBuffer(vw::byteSize(mesh.verticies), vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst, vw::MemoryPreference::GPUIO);
        auto ibo = allocator.createBuffer(vw::byteSize(mesh.indices), vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst, vw::MemoryPreference::GPUIO);
        auto ubo = allocator.createBuffer(vw::byteSize(lightInfos), vk::BufferUsageFlagBits::eUniformBuffer, vw::MemoryPreference::CPUTOGPU);

        auto albedoMap = allocator.createImage(vk::Format::eR8G8B8A8Unorm, textureFile.getExtent(), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vw::MemoryPreference::GPUIO);
        auto normalMap = allocator.createImage(vk::Format::eR8G8B8A8Unorm, normalsFile.getExtent(), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vw::MemoryPreference::GPUIO);
        auto specularMap = allocator.createImage(vk::Format::eR8G8B8A8Unorm, normalsFile.getExtent(), vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst, vw::MemoryPreference::GPUIO);
        auto gAlbedo = allocator.createImage(vk::Format::eR8G8B8A8Unorm, windowExtent, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vw::MemoryPreference::GPUIO, vk::SampleCountFlagBits::e4);
        auto gSpecular = allocator.createImage(vk::Format::eR8G8B8A8Unorm, windowExtent, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vw::MemoryPreference::GPUIO, vk::SampleCountFlagBits::e4);
        auto gPosition = allocator.createImage(vk::Format::eR16G16B16A16Sfloat, windowExtent, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vw::MemoryPreference::GPUIO, vk::SampleCountFlagBits::e4);
        auto gNormal = allocator.createImage(vk::Format::eR16G16B16A16Sfloat, windowExtent, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled, vw::MemoryPreference::GPUIO, vk::SampleCountFlagBits::e4);
        auto depthAttachment = allocator.createImage(vk::Format::eD16Unorm, windowExtent, vk::ImageUsageFlagBits::eDepthStencilAttachment, vw::MemoryPreference::GPUIO, vk::SampleCountFlagBits::e4);

        vk::DeviceSize stagingSize = mesh.byteSize() + textureFile.byteSize() + normalsFile.byteSize() + specularFile.byteSize();
        auto stagingBuffer = allocator.createBuffer(stagingSize, vk::BufferUsageFlagBits::eTransferSrc, vw::MemoryPreference::CPUTOGPU);
        const auto stagingOffsets = stagingBuffer.copyFrom(0, mesh.verticies, mesh.indices, textureFile, normalsFile, specularFile);
        
        auto transferQueue = device.getPreferredQueue({vk::QueueFlagBits::eTransfer});
        auto transferCmdPool = device.createCommandPool(vk::QueueFlagBits::eTransfer, vk::CommandPoolCreateFlagBits::eTransient);
        transferCmdPool.allocateBuffers(1);
        vw::Fence transferFinished = transferCmdPool.getBuffers()[0].oneTimeRecordAndSubmit(device, transferQueue, [&](vw::CommandBuffer cmdBuf) {
            const auto [vboOffset, iboOffset, texOffset, normalsOffset, specularOffset] = stagingOffsets;
            vbo.cmdCopyFrom(cmdBuf, stagingBuffer, vboOffset, vw::byteSize(mesh.verticies));
            ibo.cmdCopyFrom(cmdBuf, stagingBuffer, iboOffset, vw::byteSize(mesh.indices));
            albedoMap.copyFromBuffer(cmdBuf, stagingBuffer, texOffset, textureFile.getExtent());
            normalMap.copyFromBuffer(cmdBuf, stagingBuffer, normalsOffset, normalsFile.getExtent());
            specularMap.copyFromBuffer(cmdBuf, stagingBuffer, specularOffset, specularFile.getExtent());
        });

        vw::RenderPass offscreenRenderpass{
            device,
            {
                vw::RenderPass::colorAtt(vk::Format::eR8G8B8A8Unorm, true, vk::ImageLayout::eShaderReadOnlyOptimal, vk::SampleCountFlagBits::e4),
                vw::RenderPass::colorAtt(vk::Format::eR8G8B8A8Unorm, true, vk::ImageLayout::eShaderReadOnlyOptimal, vk::SampleCountFlagBits::e4),
                vw::RenderPass::colorAtt(vk::Format::eR16G16B16A16Sfloat, true, vk::ImageLayout::eShaderReadOnlyOptimal, vk::SampleCountFlagBits::e4),
                vw::RenderPass::colorAtt(vk::Format::eR16G16B16A16Sfloat, true, vk::ImageLayout::eShaderReadOnlyOptimal, vk::SampleCountFlagBits::e4),
                vw::RenderPass::depthAtt(vk::Format::eD16Unorm, false, vk::SampleCountFlagBits::e4)
            },
            {vw::RenderPass::externalColorOutputDependency, vw::RenderPass::externalDepthStencilIODependency}
        };

        vw::RenderPass deferredRenderpass{
            device,
            {vw::RenderPass::colorAtt(swapchain.getImageFormat(), true, vk::ImageLayout::ePresentSrcKHR)},
            {vw::RenderPass::externalColorOutputDependency}
        };

        std::array<vk::ClearValue, 5> clearValues;
        clearValues[0].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
        clearValues[1].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
        clearValues[2].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
        clearValues[3].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
        clearValues[4].setDepthStencil({1.0f, 0});

        auto albedoMapView = albedoMap.createView();
        auto normalMapView = normalMap.createView();
        auto specularMapView = specularMap.createView();
        auto gAlbedoView = gAlbedo.createView();
        auto gPositionView = gPosition.createView();
        auto gNormalView = gNormal.createView();
        auto gSpecularView = gSpecular.createView();
        auto depthAttachmentView = depthAttachment.createView(vk::ImageViewType::e2D, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0,1});

        vw::Sampler linearSampler{device}, nearSampler{device, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge};

        vw::DescriptorSetLayout offscreenDescriptorLayout{device, vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 3, vk::ShaderStageFlagBits::eFragment}};
        vw::DescriptorSetLayout deferredDescriptorLayout{device, {
            vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 4, vk::ShaderStageFlagBits::eFragment},
            vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment}
        }};
        vw::PipelineLayout offscreenPipelineLayout{device, offscreenDescriptorLayout, vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushData)}};
        vw::PipelineLayout deferredPipelineLayout{device, deferredDescriptorLayout, vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::vec3)}};

        vk::Viewport viewport{{},{}, static_cast<float>(windowExtent.width), static_cast<float>(windowExtent.height), 0.0f, 1.0f};
        
        vw::GraphicsPipelineBuilder offScreenPipelineBuilder{offscreenPipelineLayout, offscreenRenderpass};
        offScreenPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eVertex, offscreenVertShader.getModule());
        offScreenPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eFragment, offscreenFragShader.getModule());
        offScreenPipelineBuilder.setVertexInputState(vk::VertexInputBindingDescription{0, sizeof(vw::Vertex)}, vw::Vertex::inputAttributes);
        offScreenPipelineBuilder.setInputAssemblyState(vk::PrimitiveTopology::eTriangleList, false);
        offScreenPipelineBuilder.setViewportState(viewport, windowRect);
        offScreenPipelineBuilder.setRasterizationState(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack);
        offScreenPipelineBuilder.setBlendState({
            vw::GraphicsPipelineBuilder::noBlendAttachment,
            vw::GraphicsPipelineBuilder::noBlendAttachment,
            vw::GraphicsPipelineBuilder::noBlendAttachment,
            vw::GraphicsPipelineBuilder::noBlendAttachment
        });
        offScreenPipelineBuilder.setDepthTestState(true, true);
        offScreenPipelineBuilder.setMultisampleState(vk::SampleCountFlagBits::e4);
        vw::GraphicsPipeline offscreenPipeline{device, offScreenPipelineBuilder.getCreateInfo()};

        vw::GraphicsPipelineBuilder deferredPipelineBuilder{deferredPipelineLayout, deferredRenderpass};
        deferredPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eVertex, deferredVertShader.getModule());
        deferredPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eFragment, deferredFragShader.getModule());
        deferredPipelineBuilder.setInputAssemblyState(vk::PrimitiveTopology::eTriangleStrip, false);
        deferredPipelineBuilder.setViewportState(viewport, windowRect);
        deferredPipelineBuilder.setRasterizationState(vk::PolygonMode::eFill, vk::CullModeFlagBits::eNone);
        deferredPipelineBuilder.setBlendState(vw::GraphicsPipelineBuilder::noBlendAttachment);
        deferredPipelineBuilder.setDepthTestState(false, false);
        deferredPipelineBuilder.setMultisampleState(vk::SampleCountFlagBits::e1);
        vw::GraphicsPipeline deferredPipeline{device, deferredPipelineBuilder.getCreateInfo()};

        auto offscreenDescriptorPool = offscreenDescriptorLayout.createDedicatedPool(1);
        auto deferredDescriptorPool = deferredDescriptorLayout.createDedicatedPool(1);
        auto offscreenDescriptorSet = offscreenDescriptorPool.getSets()[0];
        auto deferredDescriptorSet = deferredDescriptorPool.getSets()[0];

        vk::DescriptorImageInfo offscreenDescriptorImageInfos[] = {
            {linearSampler, albedoMapView, vk::ImageLayout::eShaderReadOnlyOptimal},
            {linearSampler, normalMapView, vk::ImageLayout::eShaderReadOnlyOptimal},
            {linearSampler, specularMapView, vk::ImageLayout::eShaderReadOnlyOptimal}
        };
        vk::DescriptorImageInfo deferredDescriptorImageInfos[] = {
            {nearSampler, gAlbedoView, vk::ImageLayout::eShaderReadOnlyOptimal},
            {nearSampler, gSpecularView, vk::ImageLayout::eShaderReadOnlyOptimal},
            {nearSampler, gPositionView, vk::ImageLayout::eShaderReadOnlyOptimal},
            {nearSampler, gNormalView, vk::ImageLayout::eShaderReadOnlyOptimal}
        };
        vk::DescriptorBufferInfo deferredDescriptorUboInfo{ubo, 0, vw::byteSize(lightInfos)};

        device.updateDescriptorSets({
            vk::WriteDescriptorSet{offscreenDescriptorSet, 0, 0, 3, vk::DescriptorType::eCombinedImageSampler, offscreenDescriptorImageInfos},
            vk::WriteDescriptorSet{deferredDescriptorSet, 0, 0, 4, vk::DescriptorType::eCombinedImageSampler, deferredDescriptorImageInfos},
            vk::WriteDescriptorSet{deferredDescriptorSet, 1, 0, 1, vk::DescriptorType::eUniformBuffer, {}, &deferredDescriptorUboInfo}
        }, {});

        vw::Framebuffer offscreenFramebuffer{device, offscreenRenderpass, {gAlbedoView, gSpecularView, gPositionView, gNormalView, depthAttachmentView}, windowExtent};

        auto swapImageCount = vw::size32(swapchain.getImageViews());

        std::vector<vw::Framebuffer> deferredFramebuffers;
        deferredFramebuffers.reserve(swapImageCount);
        for(auto& swapView : swapchain.getImageViews())
            deferredFramebuffers.emplace_back(device, deferredRenderpass, swapView, windowExtent);

        vw::CommandPool commandPool = device.createCommandPool(mainWorkType.flags, 
                                                               vk::CommandPoolCreateFlagBits::eResetCommandBuffer | vk::CommandPoolCreateFlagBits::eTransient);
        commandPool.allocateBuffers(swapImageCount);
        vw::Semaphore imageAvailable{device}, renderingFinished{device};
        
        std::vector<vw::Fence> cmdBufferFences;
        cmdBufferFences.reserve(swapImageCount);
        for(size_t i = 0; i < swapImageCount; ++i)
            cmdBufferFences.emplace_back(device);

        window.untilClosed([&] {
            
            auto imageIndex = swapchain.getNextImageIndex(imageAvailable);
            auto commandBuffer = commandPool.getBuffers()[imageIndex];
            auto& fence = cmdBufferFences[imageIndex];

            device.waitForFences({fence, transferFinished}, VK_TRUE, 1000000);
            device.resetFences({fence}); 

            pushData.view = camera.getView();
            glm::vec3 cameraPos = camera.getPos();
            ubo.copyFrom(0, lightInfos);

            commandBuffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});
            commandBuffer.beginRenderPass(offscreenRenderpass, offscreenFramebuffer, windowRect, clearValues, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenPipeline);
            commandBuffer.pushConstants(offscreenPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(PushData), &pushData);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, offscreenPipelineLayout, 0, offscreenDescriptorSet, {});
            commandBuffer.bindVertexBuffers(0, (vk::Buffer)vbo, vk::DeviceSize{});
            commandBuffer.bindIndexBuffer(ibo, 0, vk::IndexType::eUint32);
            commandBuffer.drawIndexed(vw::size32(mesh.indices), 1, 0, 0, 0);
            commandBuffer.endRenderPass();

            commandBuffer.beginRenderPass(deferredRenderpass, deferredFramebuffers[imageIndex], windowRect, clearValues, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, deferredPipeline);
            commandBuffer.pushConstants(deferredPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0, sizeof(glm::vec3), &cameraPos);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, deferredPipelineLayout, 0, deferredDescriptorSet, {});
            commandBuffer.draw(4, 1, 0, 0);
            commandBuffer.endRenderPass();
            commandBuffer.end();

            queue.submit(vw::SubmitInfo{
                imageAvailable,
                vk::PipelineStageFlagBits::eColorAttachmentOutput,
                commandBuffer,
                renderingFinished
            }, fence);

            swapchain.present(imageIndex, {renderingFinished});
        });
        device.waitIdle();
    }
    catch (vk::SystemError& error) {
        std::cout << "vk::SystemError: " << error.what() << std::endl;
        exit(-1);
    }
    catch (std::runtime_error& err) {
        std::cout << "std::runtime_error: " << err.what() << std::endl;
        exit(-1);
    }
    catch (...) {
        std::cout << "Unknown error" << std::endl;
        exit(-1);
    }
}