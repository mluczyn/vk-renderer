#include "vkmodel.hpp"
#include <queue>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "vkutils.hpp"

constexpr auto kAssimpImportFlags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
                                    | aiProcess_GenNormals | aiProcess_CalcTangentSpace;

vw::ModelFile::ModelFile(const std::filesystem::path& modelPath) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(modelPath.string(), kAssimpImportFlags);

    if(!(scene && scene->mRootNode))
        throw std::runtime_error("Model has no root node!");

    std::queue<aiNode*> nodeQueue;
    nodeQueue.push(scene->mRootNode);
    while (nodeQueue.size()) {
        aiNode* node = nodeQueue.front();
        nodeQueue.pop();

        for (auto meshIndex : vw::ArrayProxy{node->mMeshes, node->mNumMeshes}) {
            aiMesh* mesh = scene->mMeshes[meshIndex];

            if(!(mesh->HasFaces() && mesh->HasTextureCoords(0)))
                continue;

            std::vector<vw::Vertex> verticies;
            verticies.reserve(mesh->mNumVertices);
            for(size_t i = 0; i < mesh->mNumVertices; ++i) {
                auto& pos = mesh->mVertices[i];
                auto& uv = mesh->mTextureCoords[0][i];
                auto& normal = mesh->mNormals[i];
                auto& tangent = mesh->mTangents[i];
                verticies.push_back({glm::vec3{pos.x, pos.y, pos.z},
                                     glm::vec3{normal.x, normal.y, normal.z},
                                     glm::vec3{tangent.x, tangent.y, tangent.z},
                                     glm::vec2{uv.x, 1.0f - uv.y}});
            }

            std::vector<unsigned int> indices;
            indices.reserve(3 * static_cast<size_t>(mesh->mNumFaces));
            for (auto& face : vw::ArrayProxy{mesh->mFaces, mesh->mNumFaces})
                indices.insert(indices.end(), &(face.mIndices[0]), &(face.mIndices[3]));

            mMeshes.push_back(Mesh{std::move(verticies), std::move(indices)});
        }

        for(aiNode* child : vw::ArrayProxy{node->mChildren, node->mNumChildren})
            nodeQueue.push(child);
    }



}
