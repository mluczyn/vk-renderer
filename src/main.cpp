#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <thread>

#include "shadersource.hpp"
#include "vkcamera.hpp"
#include "vkdescriptor.hpp"
#include "vkmemory.hpp"
#include "vkmodel.hpp"
#include "vkpresent.hpp"
#include "vkrender.hpp"
#include "vkshader.hpp"
#include "vktexture.hpp"
int main() {
  try {
    CameraInputHandler camera;

    vw::Instance instance{"App", 1, vw::getInstancePresentationExtensions()};

    vw::Extent windowExtent{1000, 800};
    vk::Rect2D windowRect{{0, 0}, windowExtent};
    vw::Window<CameraInputHandler> window{instance, windowExtent, "Test"};
    window.setInputHandler(camera);

    vw::QueueWorkType mainWorkType{vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, window.getSurface()};

    std::array<const std::string, 2> deviceExtensions{vw::swapchainExtension,
                                                      VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME};
    vw::Device device{instance.findPhysicalDevice(mainWorkType, vw::swapchainExtension).value(), deviceExtensions};
    auto& queue = device.getPreferredQueue(mainWorkType);

    vw::Swapchain swapchain{device, device.getPhysicalDevice(), window.getSurface(), queue};

    auto offscreenVertShader =
        vw::Shader::fromFile(device, vk::ShaderStageFlagBits::eVertex, "shaders/offscreen.vert.spv");
    auto offscreenFragShader =
        vw::Shader::fromFile(device, vk::ShaderStageFlagBits::eFragment, "shaders/offscreen.frag.spv");
    auto deferredVertShader =
        vw::Shader::fromFile(device, vk::ShaderStageFlagBits::eVertex, "shaders/deferred.vert.spv");
    auto deferredFragShader =
        vw::Shader::fromFile(device, vk::ShaderStageFlagBits::eFragment, "shaders/deferred.frag.spv");

    struct LightInfo {
      glm::vec4 pos;
      glm::vec3 color;
      float radius;
    };
    std::array<LightInfo, 2> lightInfos{
        LightInfo{glm::vec4(3.0f, -3.0f, 3.0f, 1.0f), glm::vec3(1.0f), 12.0f},
        LightInfo{glm::vec4(-3.0f, -3.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), 6.0f}};

    struct OffscreenPushData {
      glm::mat4 model;
      glm::mat4 MVP;
    };

    struct DeferredPushData {
      glm::vec3 cameraPos;
      glm::mat4 inverseVP;
    };

    glm::mat4 model = glm::scale(glm::mat4(1.0f), {0.01f, 0.01f, 0.01f});
    model = glm::rotate(model, glm::radians(180.0f), {1.0f, 0.0f, 0.0f});
    glm::mat4 proj = glm::perspective(glm::radians(70.0f), static_cast<float>(windowExtent.width / windowExtent.height),
                                      0.1f, 100.0f);
    vw::SimpleMemoryAllocator allocator{device.getPhysicalDevice(), device, 5 * 1024 * 1024};

    auto& transferQueue = device.getPreferredQueue({vk::QueueFlagBits::eTransfer});
    transferQueue.allocateOneTimeBuffers(1);

    vk::DeviceSize stagingSize = 100 * 1024 * 1024;
    vw::StagingBuffer stagingBuffer{device, allocator, stagingSize, transferQueue};
    vw::Scene scene{device, allocator, stagingBuffer, "SunTemple/SunTemple.fbx"};
    if (scene.meshes().size() == 0)
      throw std::runtime_error("Invalid model file");
    stagingBuffer.flush();

    vw::Buffer ubo{device, allocator, vw::byteSize(lightInfos), vw::BufferUse::kUniformBuffer,
                   vw::MemoryPreference::CPUTOGPU};

    vk::ImageUsageFlags gBufferUseFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
    vw::Image gAlbedo{device, allocator, vk::Format::eR8G8B8A8Unorm, windowExtent, gBufferUseFlags};
    vw::Image gSpecular{device, allocator, vk::Format::eR8G8B8A8Unorm, windowExtent, gBufferUseFlags};
    vw::Image gPosition{device, allocator, vk::Format::eR16G16B16A16Sfloat, windowExtent, gBufferUseFlags};
    vw::Image gNormal{device, allocator, vk::Format::eR16G16B16A16Sfloat, windowExtent, gBufferUseFlags};

    vw::Image depthAttachment{device, allocator, vk::Format::eD16Unorm, windowExtent,
                              vk::ImageUsageFlagBits::eDepthStencilAttachment};

    vw::RenderPass offscreenRenderpass{
        device,
        {vw::RenderPass::colorAtt(vk::Format::eR8G8B8A8Unorm, true, vk::ImageLayout::eShaderReadOnlyOptimal),
         vw::RenderPass::colorAtt(vk::Format::eR8G8B8A8Unorm, true, vk::ImageLayout::eShaderReadOnlyOptimal),
         vw::RenderPass::colorAtt(vk::Format::eR16G16B16A16Sfloat, true, vk::ImageLayout::eShaderReadOnlyOptimal),
         vw::RenderPass::colorAtt(vk::Format::eR16G16B16A16Sfloat, true, vk::ImageLayout::eShaderReadOnlyOptimal),
         vw::RenderPass::depthAtt(vk::Format::eD16Unorm)},
        {vw::RenderPass::externalColorOutputDependency, vw::RenderPass::externalDepthStencilIODependency}};

    vw::RenderPass deferredRenderpass{
        device,
        {vw::RenderPass::colorAtt(swapchain.getImageFormat(), true, vk::ImageLayout::ePresentSrcKHR)},
        {vw::RenderPass::externalColorOutputDependency}};

    std::array<vk::ClearValue, 5> clearValues;
    clearValues[0].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
    clearValues[1].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
    clearValues[2].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
    clearValues[3].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
    clearValues[4].setDepthStencil({1.0f, 0});

    auto gAlbedoView = gAlbedo.createView();
    auto gPositionView = gPosition.createView();
    auto gNormalView = gNormal.createView();
    auto gSpecularView = gSpecular.createView();
    auto depthAttachmentView =
        depthAttachment.createView(vk::ImageViewType::e2D, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});

    vw::Sampler linearSampler{device}, nearSampler{device, vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge};

    vw::DescriptorSetLayout offscreenDescriptorLayout{
        device,
        {vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eVertex},
         vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eCombinedImageSampler, 3 * 100,
                                        vk::ShaderStageFlagBits::eFragment}},
        true};
    vw::DescriptorSetLayout deferredDescriptorLayout{
        device,
        {vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eCombinedImageSampler, 4,
                                        vk::ShaderStageFlagBits::eFragment},
         vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eUniformBuffer, 1, vk::ShaderStageFlagBits::eFragment}}};
    vw::DescriptorSetLayout computeDescriptorLayout {
      device, vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 2, vk::ShaderStageFlagBits::eCompute}
    };

    vw::PipelineLayout offscreenPipelineLayout{
        device, offscreenDescriptorLayout,
        vk::PushConstantRange{vk::ShaderStageFlagBits::eVertex, 0, sizeof(OffscreenPushData)}};
    vw::PipelineLayout deferredPipelineLayout{
        device, deferredDescriptorLayout,
        vk::PushConstantRange{vk::ShaderStageFlagBits::eFragment, 0, sizeof(DeferredPushData)}};

    vk::Viewport viewport{{},   {},  static_cast<float>(windowExtent.width), static_cast<float>(windowExtent.height),
                          0.0f, 1.0f};

    vw::GraphicsPipelineBuilder offScreenPipelineBuilder{offscreenPipelineLayout, offscreenRenderpass};
    offScreenPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eVertex, offscreenVertShader.getModule());
    offScreenPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eFragment, offscreenFragShader.getModule());
    offScreenPipelineBuilder.setVertexInputState(vw::kInputBindings, vw::kInputAttributes);
    offScreenPipelineBuilder.setInputAssemblyState(vk::PrimitiveTopology::eTriangleList, false);
    offScreenPipelineBuilder.setViewportState(viewport, windowRect);
    offScreenPipelineBuilder.setRasterizationState(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack);
    offScreenPipelineBuilder.setBlendState(
        {vw::GraphicsPipelineBuilder::noBlendAttachment, vw::GraphicsPipelineBuilder::noBlendAttachment,
         vw::GraphicsPipelineBuilder::noBlendAttachment, vw::GraphicsPipelineBuilder::noBlendAttachment});
    offScreenPipelineBuilder.setDepthTestState(true, true);
    offScreenPipelineBuilder.setMultisampleState(vk::SampleCountFlagBits::e1);
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

    auto offscreenDescriptorPool = offscreenDescriptorLayout.createDedicatedPool(1, 3 * scene.materials().size());
    auto offscreenDescriptorSet = offscreenDescriptorPool.getSets()[0];
    auto deferredDescriptorPool = deferredDescriptorLayout.createDedicatedPool(1);
    auto deferredDescriptorSet = deferredDescriptorPool.getSets()[0];

    vk::DescriptorImageInfo deferredDescriptorImageInfos[] = {
        {nearSampler, gAlbedoView, vk::ImageLayout::eShaderReadOnlyOptimal},
        {nearSampler, gSpecularView, vk::ImageLayout::eShaderReadOnlyOptimal},
        {nearSampler, gPositionView, vk::ImageLayout::eShaderReadOnlyOptimal},
        {nearSampler, gNormalView, vk::ImageLayout::eShaderReadOnlyOptimal}};
    vk::DescriptorBufferInfo deferredDescriptorUboInfo{ubo, 0, vw::byteSize(lightInfos)};

    device.updateDescriptorSets(
        {vk::WriteDescriptorSet{deferredDescriptorSet, 0, 0, 4, vk::DescriptorType::eCombinedImageSampler,
                                deferredDescriptorImageInfos},
         vk::WriteDescriptorSet{
             deferredDescriptorSet, 1, 0, 1, vk::DescriptorType::eUniformBuffer, {}, &deferredDescriptorUboInfo},
         vk::WriteDescriptorSet{
             offscreenDescriptorSet, 0, 0, 1, vk::DescriptorType::eStorageBuffer, {}, scene.perModelShaderDataDesc()}},
        {});

    std::vector<vk::DescriptorImageInfo> matDescInfos;
    matDescInfos.reserve(scene.materials().size() * 3);
    for (auto& mat : scene.materials()) {
      auto descImageInfos = mat.getTextureDescriptorInfos(linearSampler);
      matDescInfos.insert(matDescInfos.end(), descImageInfos.begin(), descImageInfos.end());
    }
    device.updateDescriptorSets(
        vk::WriteDescriptorSet{offscreenDescriptorSet, 1, 0, static_cast<uint32_t>(matDescInfos.size()),
                               vk::DescriptorType::eCombinedImageSampler, matDescInfos.data()},
        {});

    vw::Framebuffer offscreenFramebuffer{device,
                                         offscreenRenderpass,
                                         {gAlbedoView, gSpecularView, gPositionView, gNormalView, depthAttachmentView},
                                         windowExtent};

    auto swapImageCount = vw::size32(swapchain.getImageViews());

    std::vector<vw::Framebuffer> deferredFramebuffers;
    deferredFramebuffers.reserve(swapImageCount);
    for (auto& swapView : swapchain.getImageViews())
      deferredFramebuffers.emplace_back(device, deferredRenderpass, swapView, windowExtent);

    vw::Semaphore imageAvailable{device}, renderingFinished{device};
    vk::PipelineStageFlags colorOutFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    queue.allocateOneTimeBuffers(swapImageCount);
    window.untilClosed([&] {
      ubo.copyToMapped(lightInfos);
      if (!queue.hasReadyBuffer())
        return;

      glm::mat4 view = camera.getView();
      glm::mat4 vp = proj * view;
      OffscreenPushData offscreenPush{model, vp * model};
      DeferredPushData deferredPush{camera.getPos(), glm::inverse(vp)};

      auto imageIndex = swapchain.getNextImageIndex(imageAvailable);
      queue.oneTimeRecordSubmit(
          [&](vw::CommandBuffer& commandBuffer) {
            commandBuffer.beginRenderPass(offscreenRenderpass, offscreenFramebuffer, windowRect, clearValues,
                                          vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenPipeline);
            commandBuffer.pushConstants(offscreenPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0,
                                        sizeof(offscreenPush), &offscreenPush);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, offscreenPipelineLayout, 0,
                                             offscreenDescriptorSet, {});
            scene.draw(commandBuffer);
            commandBuffer.endRenderPass();

            commandBuffer.beginRenderPass(deferredRenderpass, deferredFramebuffers[imageIndex], windowRect, clearValues,
                                          vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, deferredPipeline);
            commandBuffer.pushConstants(deferredPipelineLayout, vk::ShaderStageFlagBits::eFragment, 0,
                                        sizeof(deferredPush), &deferredPush);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, deferredPipelineLayout, 0,
                                             deferredDescriptorSet, {});
            commandBuffer.draw(4, 1, 0, 0);
            commandBuffer.endRenderPass();
          },
          {imageAvailable}, {colorOutFlags}, {renderingFinished});
      swapchain.present(imageIndex, {renderingFinished});
    });
    device.waitIdle();
  } catch (vk::SystemError& error) {
    std::cout << "vk::SystemError: " << error.what() << std::endl;
    exit(-1);
  } catch (std::runtime_error& err) {
    std::cout << "std::runtime_error: " << err.what() << std::endl;
    exit(-1);
  } catch (...) {
    std::cout << "Unknown error" << std::endl;
    exit(-1);
  }
}