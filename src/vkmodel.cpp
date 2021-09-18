#include "vkmodel.hpp"
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <queue>
#include "vkdds.hpp"
#include "vkutils.hpp"

static_assert(sizeof(vw::Vec3) == sizeof(aiVector3t<ai_real>));
constexpr auto kAssimpImportFlags = aiProcess_GenNormals | aiProcess_CalcTangentSpace;

static const std::unordered_map<aiTextureType, vw::TextureType> kTextureTypeMap{
    {aiTextureType_DIFFUSE, vw::TextureType::Diffuse},
    {aiTextureType_SPECULAR, vw::TextureType::Specular},
    {aiTextureType_NORMALS, vw::TextureType::Normals}};

vw::MaterialFiles convertAiMaterial(const std::filesystem::path& parentPath, const aiMaterial* aiMat) {
  vw::MaterialFiles mat;
  aiString texPath;
  for (const auto& it : kTextureTypeMap) {
    uint32_t texCount = aiMat->GetTextureCount(it.first);
    std::vector<std::filesystem::path> texVec;
    texVec.reserve(texCount);
    for (uint32_t texI = 0; texI < texCount; ++texI) {
      aiMat->GetTexture(it.first, texI, &texPath);
      texVec.emplace_back(parentPath);
      texVec.back() /= texPath.C_Str();
    }
    mat.textures[it.second] = std::move(texVec);
  }
  return mat;
}

vw::Scene::Scene(vk::Device device, vw::MemoryAllocator& allocator, vw::StagingBuffer& stagingBuf, const std::filesystem::path& modelPath) {
  Assimp::Importer importer;
  const aiScene* scene = importer.ReadFile(modelPath.string(), kAssimpImportFlags);

  if (!(scene && scene->mRootNode))
    throw std::runtime_error("Model has no root node!");

  mMaterials.reserve(scene->mNumMaterials);
  for (aiMaterial* aiMat : vw::ArrayProxy{scene->mMaterials, scene->mNumMaterials}) {
    mMaterials.emplace_back(device, allocator, convertAiMaterial(modelPath.parent_path(), aiMat));
  }

  std::vector<vw::ArrayProxy<vw::Vec3>> positions, normals, tangents, uvs;
  std::vector<uint32_t> indices, meshMatIds;
  std::vector<vk::DrawIndexedIndirectCommand> drawCommands;
  positions.reserve(scene->mNumMeshes);
  normals.reserve(scene->mNumMeshes);
  tangents.reserve(scene->mNumMeshes);
  uvs.reserve(scene->mNumMeshes);
  mMeshes.reserve(scene->mNumMeshes);
  meshMatIds.reserve(scene->mNumMeshes);
  drawCommands.reserve(scene->mNumMeshes);
  for (aiMesh* mesh : vw::ArrayProxy{scene->mMeshes, scene->mNumMeshes}) {
    if (!mesh->HasFaces() || !mesh->HasTextureCoords(0) || ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0)) {
      continue;
    }
    uint32_t indexCount = 3 * mesh->mNumFaces;
    mMeshes.push_back({indexCount, mTotalIndexCount, mTotalVertexCount, mesh->mMaterialIndex});
    drawCommands.push_back({indexCount, 1, mTotalIndexCount, static_cast<int32_t>(mTotalVertexCount), 0});
    mTotalVertexCount += mesh->mNumVertices;
    mTotalIndexCount += indexCount;
    positions.emplace_back(reinterpret_cast<vw::Vec3*>(mesh->mVertices), mesh->mNumVertices);
    normals.emplace_back(reinterpret_cast<vw::Vec3*>(mesh->mNormals), mesh->mNumVertices);
    tangents.emplace_back(reinterpret_cast<vw::Vec3*>(mesh->mTangents), mesh->mNumVertices);
    uvs.emplace_back(reinterpret_cast<vw::Vec3*>(mesh->mTextureCoords[0]), mesh->mNumVertices);
    indices.reserve(indexCount);
    for (auto& face : vw::ArrayProxy{mesh->mFaces, mesh->mNumFaces}) {
      indices.insert(indices.end(), &(face.mIndices[0]), &(face.mIndices[3]));
    }
    meshMatIds.push_back(mesh->mMaterialIndex);
  }

  mVbo.emplace(device, allocator, mTotalVertexCount * 4 * sizeof(vw::Vec3), vw::BufferUse::kVertexBuffer);
  mIbo.emplace(device, allocator, mTotalIndexCount * sizeof(uint32_t), vw::BufferUse::kIndexBuffer);

  stagingBuf.queueBufferCopies(positions, *mVbo);
  stagingBuf.queueBufferCopies(normals, *mVbo, mTotalVertexCount * sizeof(vw::Vec3));
  stagingBuf.queueBufferCopies(tangents, *mVbo, 2 * mTotalVertexCount * sizeof(vw::Vec3));
  stagingBuf.queueBufferCopies(uvs, *mVbo, 3 * mTotalVertexCount * sizeof(vw::Vec3));
  stagingBuf.queueBufferCopy(indices, *mIbo);
  for(auto& mat : mMaterials)
    mat.queueCopies(stagingBuf);

  mPerModelShaderData.emplace(device, allocator, vw::byteSize(meshMatIds), vw::BufferUse::kStorageBuffer, vw::MemoryPreference::CPUTOGPU);
  mPerModelShaderData->copyToMapped(meshMatIds);
  mPerModelShaderDataDesc = vk::DescriptorBufferInfo{*mPerModelShaderData, 0, vw::byteSize(meshMatIds)};

  mIndirectBuffer.emplace(device, allocator, vw::byteSize(drawCommands), vk::BufferUsageFlagBits::eIndirectBuffer,
                          vw::MemoryPreference::CPUTOGPU);
  mIndirectBuffer->copyToMapped(drawCommands);
  //aiLight
  /*
  std::queue<aiNode*> nodeQueue;
  nodeQueue.push(scene->mRootNode);
  while (nodeQueue.size()) {
      aiNode* node = nodeQueue.front();
      for(aiNode* child : vw::ArrayProxy{node->mChildren, node->mNumChildren})
          nodeQueue.push(child);
  }
  */
}

vw::Material::Material(vk::Device device, vw::MemoryAllocator& allocator, const MaterialFiles& files) {
  for (const auto& it : files.textures) {
    auto& curTypeVec = mTextures[it.first];
    for (const auto& path : it.second) {
      std::unique_ptr<ImageFile> texFile;
      if (path.extension() == ".dds")
        texFile = std::make_unique<vw::dds::DDSFile>(path);
      else
        texFile = std::make_unique<vw::GenericImageFile>(path);

      vw::Image texImage{device, allocator, texFile->getFormat(), texFile->getExtent(), vw::ImageUse::kTexture};
      vw::ImageView texView = texImage.createView();
      curTypeVec.emplace_back(std::move(texFile), std::move(texImage), std::move(texView));
    }
  }
}

void vw::Material::queueCopies(vw::StagingBuffer& stagingBuffer) {
  for (auto& texType : mTextures) {
    for (auto& texture : texType.second) {
      stagingBuffer.queueImageCopy(*std::get<0>(texture).get(), std::get<1>(texture));
    }
  }
}

std::array<vk::DescriptorImageInfo, 3> vw::Material::getTextureDescriptorInfos(vk::Sampler sampler) const {
  
  return std::array<vk::DescriptorImageInfo, 3>{
      vk::DescriptorImageInfo{sampler, std::get<2>(mTextures.at(vw::TextureType::Diffuse)[0]),
                              vk::ImageLayout::eShaderReadOnlyOptimal},
      vk::DescriptorImageInfo{sampler, std::get<2>(mTextures.at(vw::TextureType::Normals)[0]),
                              vk::ImageLayout::eShaderReadOnlyOptimal},
      vk::DescriptorImageInfo{sampler, std::get<2>(mTextures.at(vw::TextureType::Specular)[0]),
                              vk::ImageLayout::eShaderReadOnlyOptimal}};
}

vw::AABB computeAABB(const std::vector<vw::Vec3>& positions) {
  vw::Vec3 min = positions[0], max = positions[0];
  for(const vw::Vec3& pos : positions) {
    min = glm::min(pos, min);
    max = glm::max(pos, max);
  }
  return {glm::vec3{min.x, min.y, min.z}, glm::vec3{max.x, min.y, min.z}, glm::vec3{min.x, max.y, min.z},
          glm::vec3{min.x, min.y, max.z}, glm::vec3{max.x, max.y, min.z}, glm::vec3{max.x, min.y, max.z},
          glm::vec3{min.x, max.y, max.z}, glm::vec3{max.x, max.y, max.z}};
}