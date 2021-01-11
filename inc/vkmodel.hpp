#pragma once
#include <vector>
#include <vulkan/vulkan.hpp>
#include <glm/vec3.hpp>
#include <glm/vec2.hpp>
#include <filesystem>
#include "vktexture.hpp"

namespace vw {
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec3 tangent;
        glm::vec2 uv;
        static constexpr std::array<vk::VertexInputAttributeDescription, 4> inputAttributes{
            vk::VertexInputAttributeDescription{0, 0, vk::Format::eR32G32B32Sfloat, 0},
            vk::VertexInputAttributeDescription{1, 0, vk::Format::eR32G32B32Sfloat, sizeof(glm::vec3)},
            vk::VertexInputAttributeDescription{2, 0, vk::Format::eR32G32B32Sfloat, 2 * sizeof(glm::vec3)},
            vk::VertexInputAttributeDescription{3, 0, vk::Format::eR32G32Sfloat, 3 * sizeof(glm::vec3)}
        };
    };
    class ModelFile {
    public:
        struct Mesh {
            std::vector<Vertex> verticies;
            std::vector<uint32_t> indices;
            inline vk::DeviceSize byteSize() const {
                return verticies.size() * sizeof(Vertex) + indices.size() * sizeof(unsigned int);
            }
        };
        struct Material {
            std::vector<vw::ImageFile> diffuseTextures;
        };
        ModelFile(const std::filesystem::path& modelPath);
        inline const std::vector<Mesh>& getMeshes() const { return mMeshes; }
    private:
        std::vector<Mesh> mMeshes;
        std::vector<Material> mMaterials;
    };

};