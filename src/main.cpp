#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <iostream>
#include <json.hpp>
#include <thread>

#include "vkcamera.hpp"
#include "vkcompute.hpp"
#include "vkdescriptor.hpp"
#include "vkmemory.hpp"
#include "vkmodel.hpp"
#include "vkpresent.hpp"
#include "vkrender.hpp"
#include "vkshader.hpp"
#include "vktexture.hpp"

using json = nlohmann::json;

int main() {
  try {
    CameraInputHandler camera;

    vw::Instance instance{"App", 1, vw::getInstancePresentationExtensions(), true};

    vw::Extent windowExtent{1000, 800};
    vk::Rect2D windowRect{{0, 0}, windowExtent};
    vw::Window<CameraInputHandler> window{instance, windowExtent, "Test"};
    window.setInputHandler(camera);

    vw::QueueWorkType mainWorkType{vk::QueueFlagBits::eGraphics | vk::QueueFlagBits::eCompute, window.getSurface()};

    std::array<const std::string, 2> deviceExtensions{vw::swapchainExtension, VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME};
    vw::Device device{instance.findPhysicalDevice(mainWorkType, vw::swapchainExtension).value(), deviceExtensions};
    auto& queue = device.getPreferredQueue(mainWorkType);

    vw::Swapchain swapchain{device.getPhysicalDevice(), window.getSurface(), queue};

    struct alignas(float) LightInfo {
      glm::vec3 pos;
      float opening_angle;
      glm::vec3 color;
      float penumbra_angle;
      glm::vec3 dir;
      float _temp;
    };
    json sceneInfo;
    {
      std::ifstream i("SunTemple/SunTemple.fscene");
      i >> sceneInfo;
    }
    std::vector<LightInfo> lightInfos;
    for (auto& light : sceneInfo["lights"]) {
      if (light["type"] != "point_light")
        continue;
      auto& pos = light["pos"];
      auto& col = light["intensity"];
      auto& dir = light["direction"];

      LightInfo lightInfo;
      lightInfo.pos = glm::vec3(pos[0], pos[1], pos[2]);
      lightInfo.color = glm::vec3(col[0], col[1], col[2]);
      lightInfo.dir = glm::vec3(dir[0], dir[1], dir[2]);
      float opening_angle = glm::radians(static_cast<float>(light["opening_angle"]));
      lightInfo.opening_angle = glm::cos(opening_angle / 2.0f);
      float penumbra_angle = glm::radians(static_cast<float>(light["penumbra_angle"]));
      lightInfo.penumbra_angle = glm::cos(penumbra_angle / 2.0f);
      lightInfos.push_back(lightInfo);
    }

    struct OffscreenPushData {
      glm::mat4 VP;
    };

    struct DeferredPushData {
      glm::vec3 cameraPos;
      float pointLightRadius;
      glm::mat4 inverseVP;
    };

    vw::Shader offscreenVertShader{vk::ShaderStageFlagBits::eVertex,
                                   vw::loadShader("shaders/offscreen.vert.spv"),
                                   {{vk::DescriptorType::eStorageBuffer}, {vk::DescriptorType::eStorageBuffer}},
                                   sizeof(OffscreenPushData)};
    vw::Shader offscreenFragShader{
        vk::ShaderStageFlagBits::eFragment, vw::loadShader("shaders/offscreen.frag.spv"), {{vk::DescriptorType::eCombinedImageSampler, 400, 0, true}}};
    vw::Shader deferredCompShader{
        vk::ShaderStageFlagBits::eCompute,
        vw::loadShader("shaders/deferred.comp.spv"),
        {{vk::DescriptorType::eCombinedImageSampler, 4}, {vk::DescriptorType::eUniformBuffer}, {vk::DescriptorType::eStorageImage, 1, 1}},
        sizeof(DeferredPushData)};

    glm::mat4 model = glm::identity<glm::mat4>();
    glm::mat4 proj = glm::perspective(glm::radians(70.0f), static_cast<float>(windowExtent.width / windowExtent.height), 0.1f, 10000.0f);

    vw::MemoryAllocator allocator;

    auto& transferQueue = device.getPreferredQueue({vk::QueueFlagBits::eTransfer});
    transferQueue.allocateOneTimeBuffers(1);

    vk::DeviceSize stagingSize = 170 * 1024 * 1024;
    vw::StagingBuffer stagingBuffer{allocator, stagingSize, transferQueue};
    vw::Scene scene{allocator, stagingBuffer, "SunTemple/SunTemple.fbx"};
    if (scene.meshes().size() == 0)
      throw std::runtime_error("Invalid model file");
    stagingBuffer.flush();

    vw::Buffer ubo{allocator, vw::byteSize(lightInfos), vw::BufferUse::kUniformBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU};

    vk::ImageUsageFlags gBufferUseFlags = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled;
    vw::Image gAlbedo{allocator, vk::Format::eR8G8B8A8Unorm, windowExtent, gBufferUseFlags};
    vw::Image gSpecular{allocator, vk::Format::eR8G8B8A8Unorm, windowExtent, gBufferUseFlags};
    vw::Image gNormal{allocator, vk::Format::eR16G16B16A16Sfloat, windowExtent, gBufferUseFlags};

    vw::Image depthAttachment{allocator, vk::Format::eD32Sfloat, windowExtent,
                              vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled};

    vw::RenderPass offscreenRenderpass{{vw::RenderPass::colorAtt(vk::Format::eR8G8B8A8Unorm, true, vk::ImageLayout::eShaderReadOnlyOptimal),
                                        vw::RenderPass::colorAtt(vk::Format::eR8G8B8A8Unorm, true, vk::ImageLayout::eShaderReadOnlyOptimal),
                                        vw::RenderPass::colorAtt(vk::Format::eR16G16B16A16Sfloat, true, vk::ImageLayout::eShaderReadOnlyOptimal),
                                        vw::RenderPass::depthAtt(vk::Format::eD32Sfloat, true, vk::ImageLayout::eShaderReadOnlyOptimal)},
                                       {vw::RenderPass::externalColorOutputDependency, vw::RenderPass::externalDepthStencilIODependency}};

    std::array<vk::ClearValue, 4> clearValues;
    clearValues[0].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
    clearValues[1].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
    clearValues[2].setColor({std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}});
    clearValues[3].setDepthStencil({1.0f, 0});

    auto gAlbedoView = gAlbedo.createView();
    auto gNormalView = gNormal.createView();
    auto gSpecularView = gSpecular.createView();
    auto depthAttachmentView = depthAttachment.createView(vk::ImageViewType::e2D, {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1});

    vw::Sampler linearSampler, nearSampler{vk::Filter::eNearest, vk::SamplerAddressMode::eClampToEdge};

    vw::PipelineLayout offscreenPipelineLayout{{offscreenVertShader, offscreenFragShader}};
    vw::PipelineLayout deferredCompPipelineLayout{{deferredCompShader}};

    vk::Viewport viewport{{}, {}, static_cast<float>(windowExtent.width), static_cast<float>(windowExtent.height), 0.0f, 1.0f};

    vw::GraphicsPipelineBuilder offScreenPipelineBuilder{offscreenPipelineLayout, offscreenRenderpass};
    offScreenPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eVertex, offscreenVertShader);
    offScreenPipelineBuilder.addShaderStage(vk::ShaderStageFlagBits::eFragment, offscreenFragShader);
    offScreenPipelineBuilder.setVertexInputState(vw::kInputBindings, vw::kInputAttributes);
    offScreenPipelineBuilder.setInputAssemblyState(vk::PrimitiveTopology::eTriangleList, false);
    offScreenPipelineBuilder.setViewportState(viewport, windowRect);
    offScreenPipelineBuilder.setRasterizationState(vk::PolygonMode::eFill, vk::CullModeFlagBits::eBack);
    offScreenPipelineBuilder.setBlendState(
        {vw::GraphicsPipelineBuilder::noBlendAttachment, vw::GraphicsPipelineBuilder::noBlendAttachment, vw::GraphicsPipelineBuilder::noBlendAttachment});
    offScreenPipelineBuilder.setDepthTestState(true, true);
    offScreenPipelineBuilder.setMultisampleState(vk::SampleCountFlagBits::e1);
    vw::GraphicsPipeline offscreenPipeline{offScreenPipelineBuilder.getCreateInfo()};

    vw::ComputePipeline deferredComputePipeline{deferredCompPipelineLayout, deferredCompShader};

    auto offscreenDescriptorPool = offscreenPipelineLayout.getDescLayouts()[0].createDedicatedPool(1, 3 * scene.materials().size());
    auto offscreenDescriptorSet = offscreenDescriptorPool.getSets()[0];
    auto deferredDescriptorPool = deferredCompPipelineLayout.getDescLayouts()[0].createDedicatedPool(1);
    auto deferredDescriptorSet = deferredDescriptorPool.getSets()[0];

    vk::DescriptorImageInfo deferredDescriptorImageInfos[] = {{nearSampler, gAlbedoView, vk::ImageLayout::eShaderReadOnlyOptimal},
                                                              {nearSampler, gSpecularView, vk::ImageLayout::eShaderReadOnlyOptimal},
                                                              {nearSampler, gNormalView, vk::ImageLayout::eShaderReadOnlyOptimal},
                                                              {nearSampler, depthAttachmentView, vk::ImageLayout::eShaderReadOnlyOptimal}};
    vk::DescriptorBufferInfo deferredDescriptorUboInfo{ubo, 0, vw::byteSize(lightInfos)};

    device.updateDescriptorSets({deferredDescriptorSet.writeImages(0, vk::DescriptorType::eCombinedImageSampler, deferredDescriptorImageInfos),
                                 deferredDescriptorSet.writeBuffers(1, vk::DescriptorType::eUniformBuffer, deferredDescriptorUboInfo),
                                 offscreenDescriptorSet.writeBuffers(0, vk::DescriptorType::eStorageBuffer, scene.perMeshShaderDataDesc()),
                                 offscreenDescriptorSet.writeBuffers(1, vk::DescriptorType::eStorageBuffer, scene.modelMatrixArrayDesc())},
                                {});

    std::vector<vk::DescriptorImageInfo> matDescInfos;
    matDescInfos.reserve(scene.materials().size() * 3);
    for (auto& mat : scene.materials()) {
      auto descImageInfos = mat.getTextureDescriptorInfos(linearSampler);
      matDescInfos.insert(matDescInfos.end(), descImageInfos.begin(), descImageInfos.end());
    }
    device.updateDescriptorSets(offscreenDescriptorSet.writeImages(2, vk::DescriptorType::eCombinedImageSampler, matDescInfos), {});

    vw::Framebuffer offscreenFramebuffer{offscreenRenderpass, {gAlbedoView, gSpecularView, gNormalView, depthAttachmentView}, windowExtent};

    auto swapImageCount = vw::size32(swapchain.getImageViews());

    auto swapImageDescriptorPool = deferredCompPipelineLayout.getDescLayouts()[1].createDedicatedPool(swapImageCount);
    auto swapImageDescriptorSets = swapImageDescriptorPool.getSets();

    std::vector<vk::DescriptorImageInfo> swapImageInfos;
    for (auto swapView : swapchain.getImageViews())
      swapImageInfos.emplace_back(nullptr, swapView, vk::ImageLayout::eGeneral);
    std::vector<vk::WriteDescriptorSet> swapDescriptorWrites(swapImageCount);
    for (size_t i = 0; i < swapImageCount; ++i)
      swapDescriptorWrites[i] = swapImageDescriptorSets[i].writeImages(0, vk::DescriptorType::eStorageImage, swapImageInfos[i]);
    device.updateDescriptorSets(swapDescriptorWrites, {});

    vw::Semaphore imageAvailable, renderingFinished;
    vk::PipelineStageFlags colorOutFlags = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    queue.allocateOneTimeBuffers(swapImageCount);
    window.untilClosed([&] {
      ubo.copyToMapped(lightInfos);
      if (!queue.hasReadyBuffer())
        return;

      glm::mat4 view = camera.getView();
      glm::mat4 vp = proj * view;
      OffscreenPushData offscreenPush{vp};
      DeferredPushData deferredPush{camera.getPos(), 1.0f, glm::inverse(vp)};

      auto imageIndex = swapchain.getNextImageIndex(imageAvailable);
      queue.oneTimeRecordSubmit(
          [&](vw::CommandBuffer& commandBuffer) {
            commandBuffer.beginRenderPass(offscreenRenderpass, offscreenFramebuffer, windowRect, clearValues, vk::SubpassContents::eInline);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, offscreenPipeline);
            commandBuffer.pushConstants(offscreenPipelineLayout, vk::ShaderStageFlagBits::eVertex, 0, sizeof(offscreenPush), &offscreenPush);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, offscreenPipelineLayout, 0, {offscreenDescriptorSet}, {});
            scene.draw(commandBuffer);
            commandBuffer.endRenderPass();

            vw::Image::transitionLayout(commandBuffer, swapchain.getImage(imageIndex), vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral);
            commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, deferredComputePipeline);
            commandBuffer.pushConstants(deferredCompPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(deferredPush), &deferredPush);
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, deferredCompPipelineLayout, 0,
                                             {deferredDescriptorSet, swapImageDescriptorSets[imageIndex]}, {});
            commandBuffer.dispatch(windowExtent.width, windowExtent.height, 1);
            vw::Image::transitionLayout(commandBuffer, swapchain.getImage(imageIndex), vk::ImageLayout::eGeneral, vk::ImageLayout::ePresentSrcKHR);
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