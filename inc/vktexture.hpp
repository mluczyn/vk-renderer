#pragma once
#include <filesystem>
#include <vulkan/vulkan.hpp>
#include "vkutils.hpp"

namespace vw {

class ImageFile {
public:
    using value_type = byte;
    ImageFile(const std::filesystem::path& path, int requiredCompCount = 3);
    ~ImageFile();
    inline vk::Extent3D getExtent() const {return mExtent;}
    inline vk::DeviceSize byteSize() const {return mSize;}
    inline void copyTo(byte* dest) const {
        std::copy(mDataStart, mDataStart + mSize, dest);
    }
    inline byte* begin() const { return mDataStart; }
    inline byte* end() const {return mDataStart+mSize; }
    inline vk::DeviceSize size() const {return mSize;}
private:
    byte* mDataStart = nullptr;
    vk::DeviceSize mSize;
    vk::Extent3D mExtent;
};

class Sampler : public vw::HandleContainerUnique<vk::Sampler> {
public:
    Sampler(vk::Device device, vk::Filter filter = vk::Filter::eLinear, 
            vk::SamplerAddressMode addressMode = vk::SamplerAddressMode::eRepeat, float maxAnisotropy = 16.0f);
};

}