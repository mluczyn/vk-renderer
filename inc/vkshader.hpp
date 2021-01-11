#pragma once
#include <filesystem>
#include <string>
#include "vulkan/vulkan.hpp"

namespace vw {
    class Shader {
    public:
        static vw::Shader fromFile(vk::Device& device, vk::ShaderStageFlagBits stage, const std::filesystem::path& path);
        Shader(vk::Device& device, vk::ShaderStageFlagBits stage, const std::string& text);
        ~Shader();
        inline vk::ShaderModule getModule() {
            return mModule;
        }
    private:
        vk::Device mDeviceHandle;
        vk::ShaderModule mModule;
    };
}