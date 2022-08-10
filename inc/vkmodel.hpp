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
enum TextureType { Diffuse, Normals, Specular, MaxEnum };

struct MaterialFiles {
  std::unordered_map<TextureType, std::filesystem::path> textures;
};

static constexpr std::array<vk::VertexInputBindingDescription, 4> kInputBindings{
    vk::VertexInputBindingDescription{0, sizeof(vw::Vec3)}, vk::VertexInputBindingDescription{1, sizeof(vw::Vec3)},
    vk::VertexInputBindingDescription{2, sizeof(vw::Vec3)}, vk::VertexInputBindingDescription{3, sizeof(vw::Vec3)}};

static constexpr std::array<vk::VertexInputAttributeDescription, 4> kInputAttributes{
    vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0}, vk::VertexInputAttributeDescription{1, 1, vk::Format::eR32G32B32Sfloat, 0},
    vk::VertexInputAttributeDescription{2, 2, vk::Format::eR32G32B32Sfloat, 0}, vk::VertexInputAttributeDescription{3, 3, vk::Format::eR32G32B32Sfloat, 0}};

struct MeshInfo {
  uint32_t indexCount = 0;
  uint32_t firstIndex = 0;
  uint32_t vertexOffset = 0;
  uint32_t materialIndex = 0;
};

class Material {
 public:
  Material(vw::MemoryAllocator& allocator, const MaterialFiles& files);
  Material(const Material& other) = delete;
  Material& operator=(const Material& other) = delete;
  void queueCopies(vw::StagingBuffer& stagingBuffer);
  std::array<vk::DescriptorImageInfo, 3> getTextureDescriptorInfos(vk::Sampler sampler) const;

 private:
  struct Texture {
    std::optional<std::unique_ptr<vw::ImageFile>> imageFile;
    std::optional<vw::Image> image;
    std::optional<vw::ImageView> imageView;
  };
  std::array<Texture, vw::TextureType::MaxEnum> mTextures;
};

class Scene {
 public:
  Scene(vw::MemoryAllocator& allocator, vw::StagingBuffer& stagingBuf, const std::filesystem::path& modelPath);
  const std::vector<MeshInfo>& meshes() const {
    return mMeshes;
  }
  const FixedVec<Material>& materials() const {
    return mMaterials.value();
  }
  const vk::DescriptorBufferInfo& perMeshShaderDataDesc() const {
    return mPerMeshShaderDataDesc;
  }
  const vk::DescriptorBufferInfo& modelMatrixArrayDesc() const {
    return mModelMatrixArrayDesc;
  }
  void draw(vk::CommandBuffer cmdBuf) const {
    uint32_t vboSectionSize = mTotalVertexCount * sizeof(vw::Vec3);
    cmdBuf.bindVertexBuffers(0, {*mVbo, *mVbo, *mVbo, *mVbo}, {0, vboSectionSize, 2 * vboSectionSize, 3 * vboSectionSize});
    cmdBuf.bindIndexBuffer(*mIbo, 0, vk::IndexType::eUint32);
    cmdBuf.drawIndexedIndirect(*mIndirectBuffer, 0, mMeshes.size(), sizeof(vk::DrawIndexedIndirectCommand));
  }

 private:
  struct PerMeshData {
    uint32_t materialIdx, modelMatrixBaseIndex;
  };

  std::optional<vw::Buffer> mVbo, mIbo, mIndirectBuffer, mUbo;
  std::vector<MeshInfo> mMeshes;
  std::optional<vw::FixedVec<Material>> mMaterials;
  vk::DescriptorBufferInfo mPerMeshShaderDataDesc, mModelMatrixArrayDesc;
  uint32_t mTotalVertexCount = 0, mTotalIndexCount = 0, mTotalInstanceCount = 0;
};
}  // namespace vw