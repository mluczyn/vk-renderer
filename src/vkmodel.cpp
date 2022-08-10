#include "vkmodel.hpp"
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_operation.hpp>
#include <glm/mat4x4.hpp>
#include <queue>
#include <stack>
#include "vkdds.hpp"
#include "vkutils.hpp"

static_assert(sizeof(vw::Vec3) == sizeof(aiVector3t<ai_real>));

static const std::unordered_map<aiTextureType, vw::TextureType> kTextureTypeMap{{aiTextureType_DIFFUSE, vw::TextureType::Diffuse},
                                                                                {aiTextureType_SPECULAR, vw::TextureType::Specular},
                                                                                {aiTextureType_NORMALS, vw::TextureType::Normals}};

vw::MaterialFiles convertAiMaterial(const std::filesystem::path& parentPath, const aiMaterial* aiMat) {
  vw::MaterialFiles mat;
  aiString texPath;
  for (const auto& it : kTextureTypeMap) {
    uint32_t texCount = aiMat->GetTextureCount(it.first);
    if (texCount > 0) {
      aiMat->GetTexture(it.first, 0, &texPath);
      std::filesystem::path texFullPath = parentPath;
      texFullPath /= texPath.C_Str();
      mat.textures.emplace(it.second, texFullPath);
    }
  }
  return mat;
}

vw::Scene::Scene(vw::MemoryAllocator& allocator, vw::StagingBuffer& stagingBuf, const std::filesystem::path& modelPath) {
  uint32_t importFlags = aiProcessPreset_TargetRealtime_Quality;
  importFlags |= aiProcess_CalcTangentSpace;
  importFlags |= aiProcess_RemoveComponent;
  importFlags &= ~aiProcess_FindDegenerates;
  importFlags &= ~aiProcess_OptimizeGraph;
  importFlags &= ~aiProcess_RemoveRedundantMaterials;
  importFlags &= ~aiProcess_SplitLargeMeshes;

  Assimp::Importer importer;
  importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_COLORS);
  const aiScene* scene = importer.ReadFile(modelPath.string(), importFlags);

  if (!(scene && scene->mRootNode))
    throw std::runtime_error("Model has no root node!");

  mMaterials.emplace(scene->mNumMaterials);
  for (aiMaterial* aiMat : vw::ArrayProxy{scene->mMaterials, scene->mNumMaterials}) {
    mMaterials->emplace_back(allocator, convertAiMaterial(modelPath.parent_path(), aiMat));
  }

  std::vector<std::vector<glm::mat4>> instanceTransforms(scene->mNumMeshes);
  std::stack<std::tuple<aiNode*, glm::mat4>> nodeTreeDfs;
  nodeTreeDfs.push(std::make_tuple(scene->mRootNode, glm::diagonal4x4(glm::vec4{1.0, 1.0, 1.0, 1.0})));
  while (!nodeTreeDfs.empty()) {
    auto [curNode, parentMatrix] = nodeTreeDfs.top();
    nodeTreeDfs.pop();
    auto& curT = curNode->mTransformation;
    glm::mat4 curMatrix{
        curT.a1, curT.a2, curT.a3, curT.a4, curT.b1, curT.b2, curT.b3, curT.b4, curT.c1, curT.c2, curT.c3, curT.c4, curT.d1, curT.d2, curT.d3, curT.d4,
    };
    curMatrix = glm::transpose(curMatrix);
    curMatrix = parentMatrix * curMatrix;

    mTotalInstanceCount += curNode->mNumMeshes;
    for (auto meshIdx : vw::ArrayProxy{curNode->mMeshes, curNode->mNumMeshes})
      instanceTransforms[meshIdx].push_back(curMatrix);
    for (auto childNode : vw::ArrayProxy{curNode->mChildren, curNode->mNumChildren})
      nodeTreeDfs.push(std::make_tuple(childNode, curMatrix));
  }

  std::vector<vw::ArrayProxy<vw::Vec3>> positions, normals, tangents, uvs;
  std::vector<PerMeshData> perMeshData;
  std::vector<uint32_t> indices;
  std::vector<glm::mat4> meshMatrices;
  std::vector<vk::DrawIndexedIndirectCommand> drawCommands;
  positions.reserve(scene->mNumMeshes);
  normals.reserve(scene->mNumMeshes);
  tangents.reserve(scene->mNumMeshes);
  uvs.reserve(scene->mNumMeshes);
  mMeshes.reserve(scene->mNumMeshes);
  perMeshData.reserve(scene->mNumMeshes);
  meshMatrices.reserve(mTotalInstanceCount);
  drawCommands.reserve(scene->mNumMeshes);

  for (size_t meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx) {
    const aiMesh* mesh = scene->mMeshes[meshIdx];
    if (!mesh->HasFaces() || !mesh->HasTextureCoords(0) || ((mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE) == 0)) {
      continue;
    }
    uint32_t indexCount = 3 * mesh->mNumFaces;
    mMeshes.push_back({indexCount, mTotalIndexCount, mTotalVertexCount, mesh->mMaterialIndex});
    drawCommands.push_back({indexCount, vw::size32(instanceTransforms[meshIdx]), mTotalIndexCount, static_cast<int32_t>(mTotalVertexCount), 0});
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

    perMeshData.push_back({mesh->mMaterialIndex, vw::size32(meshMatrices)});
    auto& curMeshMatrices = instanceTransforms[meshIdx];
    meshMatrices.insert(meshMatrices.end(), curMeshMatrices.begin(), curMeshMatrices.end());
  }

  mVbo.emplace(allocator, mTotalVertexCount * 4 * sizeof(vw::Vec3), vw::BufferUse::kVertexBuffer);
  mIbo.emplace(allocator, mTotalIndexCount * sizeof(uint32_t), vw::BufferUse::kIndexBuffer);

  stagingBuf.queueBufferCopies(positions, *mVbo);
  stagingBuf.queueBufferCopies(normals, *mVbo, mTotalVertexCount * sizeof(vw::Vec3));
  stagingBuf.queueBufferCopies(tangents, *mVbo, 2 * mTotalVertexCount * sizeof(vw::Vec3));
  stagingBuf.queueBufferCopies(uvs, *mVbo, 3 * mTotalVertexCount * sizeof(vw::Vec3));
  stagingBuf.queueBufferCopy(indices, *mIbo);
  for (auto& mat : mMaterials.value())
    mat.queueCopies(stagingBuf);

  mUbo.emplace(allocator, std::initializer_list<vk::DeviceSize>{vw::byteSize(perMeshData), vw::byteSize(meshMatrices)}, vw::BufferUse::kStorageBuffer,
               VMA_MEMORY_USAGE_CPU_TO_GPU);
  mUbo->copyToMapped(perMeshData, 0);
  mUbo->copyToMapped(meshMatrices, 1);

  mIndirectBuffer.emplace(allocator, vw::byteSize(drawCommands), vk::BufferUsageFlagBits::eIndirectBuffer, VMA_MEMORY_USAGE_CPU_TO_GPU);
  mIndirectBuffer->copyToMapped(drawCommands);

  mPerMeshShaderDataDesc = mUbo->getSegmentDesc(0);
  mModelMatrixArrayDesc = mUbo->getSegmentDesc(1);
}

vw::Material::Material(vw::MemoryAllocator& allocator, const MaterialFiles& files) {
  for (auto i = 0; i < vw::TextureType::MaxEnum; ++i) {
    auto texType = static_cast<vw::TextureType>(i);
    auto& tex = mTextures[i];

    if (files.textures.count(texType)) {
      auto& path = files.textures.at(texType);
      if (path.extension() == ".dds")
        tex.imageFile = std::make_unique<vw::dds::DDSFile>(path);
      else
        tex.imageFile = std::make_unique<vw::GenericImageFile>(path);
    } else
      tex.imageFile = std::make_unique<vw::DefaultValueFile<glm::vec4>>(glm::vec4{0.0f, 0.0f, 0.0f, 1.0f}, vk::Format::eR8G8B8A8Unorm);

    tex.image.emplace(allocator, tex.imageFile->get()->getFormat(), tex.imageFile->get()->getExtent(), vw::ImageUse::kTexture);
    tex.imageView.emplace(tex.image->createView());
  }
}

void vw::Material::queueCopies(vw::StagingBuffer& stagingBuffer) {
  for (auto& texture : mTextures)
    stagingBuffer.queueImageCopy(*(texture.imageFile->get()), texture.image.value());
}

std::array<vk::DescriptorImageInfo, 3> vw::Material::getTextureDescriptorInfos(vk::Sampler sampler) const {
  return {vk::DescriptorImageInfo{sampler, mTextures[vw::TextureType::Diffuse].imageView.value(), vk::ImageLayout::eShaderReadOnlyOptimal},
          vk::DescriptorImageInfo{sampler, mTextures[vw::TextureType::Normals].imageView.value(), vk::ImageLayout::eShaderReadOnlyOptimal},
          vk::DescriptorImageInfo{sampler, mTextures[vw::TextureType::Specular].imageView.value(), vk::ImageLayout::eShaderReadOnlyOptimal}};
}

vw::AABB computeAABB(const std::vector<vw::Vec3>& positions) {
  vw::Vec3 min = positions[0], max = positions[0];
  for (const vw::Vec3& pos : positions) {
    min = glm::min(pos, min);
    max = glm::max(pos, max);
  }
  return {glm::vec3{min.x, min.y, min.z}, glm::vec3{max.x, min.y, min.z}, glm::vec3{min.x, max.y, min.z}, glm::vec3{min.x, min.y, max.z},
          glm::vec3{max.x, max.y, min.z}, glm::vec3{max.x, min.y, max.z}, glm::vec3{min.x, max.y, max.z}, glm::vec3{max.x, max.y, max.z}};
}