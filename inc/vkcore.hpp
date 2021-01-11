#pragma once
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <optional>
#include <vulkan\vulkan.hpp>
#include <cassert>
#include "vkutils.hpp"

namespace vw
{
    constexpr std::array<const char*, 1> DebugValidationLayers = {
        "VK_LAYER_LUNARG_standard_validation"
    };
    
    class Extent {
    public:
        Extent(uint32_t width, uint32_t height, uint32_t depth = 1) : width{width}, height{height}, depth{depth} {}
        inline operator vk::Extent2D() const {return {width, height}; };
        inline operator vk::Extent3D() const {return {width, height, depth}; };
        uint32_t width, height, depth;
    };
    class Semaphore : public vw::HandleContainerUnique<vk::Semaphore> {
    public:
        Semaphore(vk::Device device);
    };
    
    class Fence : public vw::HandleContainerUnique<vk::Fence> {
    public:
        Fence(vk::Device device);
    };
    
    struct SubmitInfo {
        ArrayProxy<vk::Semaphore> waitSemaphores;
        ArrayProxy<vk::PipelineStageFlags> waitDstStageMasks;
        ArrayProxy<vk::CommandBuffer> commandBuffers;
        ArrayProxy<vk::Semaphore> signalSemaphores;
    };

    class Queue : public vk::Queue {
    public:
        inline void submit(ArrayProxy<vw::SubmitInfo> submitInfos, vk::Fence fence = {}) {
            std::vector<vk::SubmitInfo> parsedSubmitInfos;
            parsedSubmitInfos.reserve(submitInfos.size());
            for (auto& submitInfo : submitInfos) {
                parsedSubmitInfos.push_back(vk::SubmitInfo{
                    submitInfo.waitSemaphores.size(),
                    submitInfo.waitSemaphores.data(),
                    submitInfo.waitDstStageMasks.data(),
                    submitInfo.commandBuffers.size(),
                    submitInfo.commandBuffers.data(),
                    submitInfo.signalSemaphores.size(),
                    submitInfo.signalSemaphores.data()
                    });
            }
            vk::Queue::submit(parsedSubmitInfos, fence);
        }
    };

    class CommandBuffer : public vk::CommandBuffer {
    public:
        CommandBuffer(vk::CommandBuffer commandBuffer) : vk::CommandBuffer{commandBuffer} {}
        inline void begin(vk::CommandBufferUsageFlags flags = {}) const {
            vk::CommandBuffer::begin(vk::CommandBufferBeginInfo{flags});
        }
        template <typename T>
        inline void record(vk::CommandBufferUsageFlags flags, T recordFunc) const {
            vk::CommandBuffer::begin(vk::CommandBufferBeginInfo{flags});
            recordFunc(*this);
            vk::CommandBuffer::end();
        }
        template <typename T>
        inline vw::Fence oneTimeRecordAndSubmit(vk::Device device, vw::Queue queue, T recordFunc) const {
            vw::Fence fence{device};
            record(vk::CommandBufferUsageFlagBits::eOneTimeSubmit, recordFunc);
            queue.submit(vw::SubmitInfo{{}, {}, *this, {}}, fence);
            return std::move(fence);
        }
        inline void beginSecondary(vk::CommandBufferUsageFlags flags, const vk::CommandBufferInheritanceInfo& inheritanceInfo) const {
            vk::CommandBuffer::begin(vk::CommandBufferBeginInfo{flags, &inheritanceInfo});
        }
        inline void beginRenderPass(vk::RenderPass renderPass, vk::Framebuffer framebuffer, const vk::Rect2D& renderArea, ArrayProxy<vk::ClearValue> clearValues,
                                    vk::SubpassContents subpassContents) const {
            vk::CommandBuffer::beginRenderPass(vk::RenderPassBeginInfo{
                renderPass,
                framebuffer,
                renderArea,
                clearValues.size(),
                clearValues.data()
            }, subpassContents);
        }
    };

    class CommandPool : public vw::HandleContainerUnique<vk::CommandPool> {
    public:
        CommandPool(vk::CommandPool poolhandle, vk::Device deviceHandle);
        void allocateBuffers(uint32_t count);
        inline const std::vector<vw::CommandBuffer>& getBuffers() {
            return mBuffers;
        }
        inline void reset(bool releaseResources = false) {
            mDeviceHandle.resetCommandPool(mHandle, releaseResources ? vk::CommandPoolResetFlagBits::eReleaseResources : vk::CommandPoolResetFlags{});
        }
    private:
        std::vector<vw::CommandBuffer> mBuffers;
    };

    struct QueueWorkType {
        vk::QueueFlags flags;
        vk::SurfaceKHR surfaceSupport = {};
    };

    class PhysicalDevice : public vk::PhysicalDevice {
    public:
        PhysicalDevice(vk::PhysicalDevice physicalDevice);
        bool queryWorkTypeSupport(const QueueWorkType& workType) const;
        bool queryExtensionSupport(ArrayProxy<const std::string> extensions) const;
    private:
        std::vector<std::string> mSupportedExtensions;
        std::vector<vk::QueueFamilyProperties> mQueueFamilyProperties;
    };

    class Instance : public vk::Instance
    {
    public:
        Instance(std::string appName, uint32_t version, std::vector<const char*> platformExtensions = {});
        ~Instance();
        std::optional<vk::PhysicalDevice> findPhysicalDevice(ArrayProxy<QueueWorkType> workTypes, ArrayProxy<const std::string> extensions) const;
    private:
        bool checkValidationLayerSupport();
        bool checkExtensionSupport(std::vector<const char*> extensions);
        std::vector<vw::PhysicalDevice> mPhysicalDevices;
        vk::DebugUtilsMessengerEXT mDebugMessenger;
    };

    class Device : public vk::Device
    {
    public:
        Device(vk::PhysicalDevice physicalDevice, ArrayProxy<const std::string> extensions);
        ~Device();
        inline vk::PhysicalDevice getPhysicalDevice() { return mPhysicalDeviceHandle; };
        inline vw::Queue getPreferredQueue(const QueueWorkType& workType) const {
            return mQueues[getPreferredQueueFamily(workType)];
        }
        void waitIdle();
        vw::CommandPool createCommandPool(vk::QueueFlags queueFamilyFlags, vk::CommandPoolCreateFlags poolCreateFlags = {});
    private:
        uint32_t getPreferredQueueFamily(const QueueWorkType& workType) const;
        vk::PhysicalDevice mPhysicalDeviceHandle;
        vk::PhysicalDeviceFeatures mDeviceFeatures;

        std::vector<vk::QueueFamilyProperties> mQueueFamilies;
        std::vector<vw::Queue> mQueues;
    };
}