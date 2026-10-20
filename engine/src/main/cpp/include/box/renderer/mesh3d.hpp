// Path: engine/src/main/cpp/include/box/renderer/mesh3d.hpp
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "box/math/math.hpp"

namespace box::renderer {

/** Interleaved CPU vertex layout shared by glTF loading and GLES 3.0 mesh upload. */
struct Vertex3D {
    math::Vec3 position{};
    math::Vec3 normal{0.0f, 1.0f, 0.0f};
    math::Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    math::Vec2 texcoord{};
};

struct GltfSampler {
    std::int32_t minFilter = 9729;
    std::int32_t magFilter = 9729;
    std::int32_t wrapS = 10497;
    std::int32_t wrapT = 10497;
};

struct GltfImage {
    std::string name;
    std::string mimeType;
    std::vector<std::uint8_t> encodedBytes;
};

struct GltfTexture {
    std::int32_t imageIndex = -1;
    std::int32_t samplerIndex = -1;
};

enum class GltfAlphaMode : std::uint8_t { Opaque, Mask, Blend };

/** glTF metallic-roughness material inputs, retaining source texture indices. */
struct GltfMaterial {
    std::string name;
    math::Vec4 baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float alphaCutoff = 0.5f;
    std::int32_t baseColorTexture = -1;
    std::int32_t metallicRoughnessTexture = -1;
    std::int32_t normalTexture = -1;
    GltfAlphaMode alphaMode = GltfAlphaMode::Opaque;
    bool doubleSided = false;
};

struct MeshPrimitive3D {
    std::vector<Vertex3D> vertices;
    std::vector<std::uint32_t> indices;
    math::AABB bounds{};
    std::int32_t materialIndex = -1;
};

struct Mesh3D {
    std::string name;
    std::vector<MeshPrimitive3D> primitives;
};

/** A node retains its exact local matrix and optional mesh reference. */
struct GltfNode {
    std::string name;
    math::Mat4 localTransform = math::Mat4::identity();
    std::int32_t meshIndex = -1;
    std::int32_t parentIndex = -1;
    std::vector<std::uint32_t> children;
};

struct GltfModel {
    std::vector<Mesh3D> meshes;
    std::vector<GltfMaterial> materials;
    std::vector<GltfSampler> samplers;
    std::vector<GltfImage> images;
    std::vector<GltfTexture> textures;
    std::vector<GltfNode> nodes;
    std::vector<std::vector<std::uint32_t>> scenes;
    std::int32_t defaultScene = -1;

    [[nodiscard]] bool evaluateWorldTransforms(std::vector<math::Mat4>& output,
                                                std::string* error = nullptr) const;
};

struct VisibleGltfPrimitive {
    std::uint32_t nodeIndex = 0U;
    std::uint32_t meshIndex = 0U;
    std::uint32_t primitiveIndex = 0U;
    math::Mat4 worldTransform = math::Mat4::identity();
};

/** Appends the default scene's primitives whose transformed AABBs intersect the view frustum. */
[[nodiscard]] bool collectVisibleGltfPrimitives(const GltfModel& model,
                                                 const math::Frustum& frustum,
                                                 std::vector<VisibleGltfPrimitive>& output,
                                                 std::string* error = nullptr);

}  // namespace box::renderer
