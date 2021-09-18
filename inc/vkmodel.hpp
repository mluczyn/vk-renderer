#pragma once
#include <filesystem>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <vector>
#include <vulkan/vulkan.hpp>
#include "vkmemory.hpp"
#include "vktexture.hpp"

namespace vw {
using Vec2 = glm::vec2;
using Vec3 = glm::vec3;
using AABB = std::array<Vec3, 8>;
enum class TextureType { Diffuse, Specular, Normals };

struct MaterialFiles {
  std::unordered_map<TextureType, std::vector<std::filesystem::path>> textures;
};

static constexpr std::array<vk::VertexInputBindingDescription, 4> kInputBindings{
    vk::VertexInputBindingDescription{0, sizeof(vw::Vec3)}, vk::VertexInputBindingDescription{1, sizeof(vw::Vec3)},
    vk::VertexInputBindingDescription{2, sizeof(vw::Vec3)}, vk::VertexInputBindingDescription{3, sizeof(vw::Vec3)}};

static constexpr std::array<vk::VertexInputAttributeDescription, 4> kInputAttributes{
    vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
    vk::VertexInputAttributeDescription{1, 1, vk::Format::eR32G32B32Sfloat, 0},
    vk::VertexInputAttributeDescription{2, 2, vk::Format::eR32G32B32Sfloat, 0},
    vk::VertexInputAttributeDescription{3, 3, vk::Format::eR32G32B32Sfloat, 0}};

struct MeshInfo {
  uint32_t indexCount = 0;
  uint32_t firstIndex = 0;
  uint32_t vertexOffset = 0;
  uint32_t materialIndex = 0;
};

class Material {
 public:
  Material(vk::Device device, vw::MemoryAllocator& allocator, const MaterialFiles& files);
  Material(const Material&) = delete;
  Material(Material&&) = default;
  void queueCopies(vw::StagingBuffer& stagingBuffer);
  std::array<vk::DescriptorImageInfo, 3> getTextureDescriptorInfos(vk::Sampler sampler) const;

 private:
  vk::Device mDeviceHandle;
  using Texture = std::tuple<std::unique_ptr<vw::ImageFile>, vw::Image, vw::ImageView>;
  std::unordered_map<TextureType, std::vector<Texture>> mTextures;
};

class Scene {
 public:
  Scene(vk::Device device,
        vw::MemoryAllocator& allocator,
        vw::StagingBuffer& stagingBuf,
        const std::filesystem::path& modelPath);
  const std::vector<MeshInfo>& meshes() const {
    return mMeshes;
  }
  const std::vector<Material>& materials() const {
    return mMaterials;
  }
  const vk::DescriptorBufferInfo* perModelShaderDataDesc() const {
    return &mPerModelShaderDataDesc;
  }
  void draw(vk::CommandBuffer cmdBuf) const {
    uint32_t vboSectionSize = mTotalVertexCount * sizeof(vw::Vec3);
    cmdBuf.bindVertexBuffers(0, {*mVbo, *mVbo, *mVbo, *mVbo},
                             {0, vboSectionSize, 2 * vboSectionSize, 3 * vboSectionSize});
    cmdBuf.bindIndexBuffer(*mIbo, 0, vk::IndexType::eUint32);
    cmdBuf.drawIndexedIndirect(*mIndirectBuffer,0, mMeshes.size(), sizeof(vk::DrawIndexedIndirectCommand));
  }
 private:
  std::optional<vw::Buffer> mVbo, mIbo, mIndirectBuffer, mPerModelShaderData;
  vk::DescriptorBufferInfo mPerModelShaderDataDesc;
  std::vector<MeshInfo> mMeshes;
  std::vector<Material> mMaterials;
  uint32_t mTotalVertexCount = 0, mTotalIndexCount = 0;
};
};  // namespace vw