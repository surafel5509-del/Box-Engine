// Path: engine/src/main/cpp/src/renderer/mesh3d.cpp
#include "box/renderer/mesh3d.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace box::renderer {
namespace {

void setError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

[[nodiscard]] math::AABB transformBounds(const math::AABB& bounds,
                                         const math::Mat4& transform) {
    const std::array<math::Vec3, 8> corners = {
        math::Vec3{bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
        math::Vec3{bounds.maximum.x, bounds.minimum.y, bounds.minimum.z},
        math::Vec3{bounds.minimum.x, bounds.maximum.y, bounds.minimum.z},
        math::Vec3{bounds.maximum.x, bounds.maximum.y, bounds.minimum.z},
        math::Vec3{bounds.minimum.x, bounds.minimum.y, bounds.maximum.z},
        math::Vec3{bounds.maximum.x, bounds.minimum.y, bounds.maximum.z},
        math::Vec3{bounds.minimum.x, bounds.maximum.y, bounds.maximum.z},
        math::Vec3{bounds.maximum.x, bounds.maximum.y, bounds.maximum.z},
    };
    math::AABB result{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}};
    bool firstPoint = true;
    for (const math::Vec3 corner : corners) {
        const math::Vec4 transformed = transform.transform({corner.x, corner.y, corner.z, 1.0f});
        const float inverseW = std::fabs(transformed.w) > math::kEpsilon
                                  ? 1.0f / transformed.w
                                  : 1.0f;
        const math::Vec3 point{transformed.x * inverseW,
                               transformed.y * inverseW,
                               transformed.z * inverseW};
        if (firstPoint) {
            result.minimum = point;
            result.maximum = point;
            firstPoint = false;
        } else {
            result.minimum.x = std::min(result.minimum.x, point.x);
            result.minimum.y = std::min(result.minimum.y, point.y);
            result.minimum.z = std::min(result.minimum.z, point.z);
            result.maximum.x = std::max(result.maximum.x, point.x);
            result.maximum.y = std::max(result.maximum.y, point.y);
            result.maximum.z = std::max(result.maximum.z, point.z);
        }
    }
    return result;
}

}  // namespace

bool GltfModel::evaluateWorldTransforms(std::vector<math::Mat4>& output,
                                        std::string* error) const {
    constexpr std::uint8_t kUnvisited = 0U;
    constexpr std::uint8_t kVisiting = 1U;
    constexpr std::uint8_t kVisited = 2U;
    std::vector<std::uint8_t> states(nodes.size(), kUnvisited);
    std::vector<math::Mat4> candidate(nodes.size(), math::Mat4::identity());

    const auto evaluate = [&](const auto& self, std::size_t index) -> bool {
        if (index >= nodes.size()) {
            setError(error, "A glTF node references an out-of-range parent or child.");
            return false;
        }
        if (states[index] == kVisited) return true;
        if (states[index] == kVisiting) {
            setError(error, "The glTF node hierarchy contains a cycle.");
            return false;
        }
        states[index] = kVisiting;
        const GltfNode& node = nodes[index];
        if (node.parentIndex >= 0) {
            const std::size_t parent = static_cast<std::size_t>(node.parentIndex);
            if (parent >= nodes.size() || !self(self, parent)) {
                if (error != nullptr && error->empty()) {
                    setError(error, "A glTF node has an invalid parent reference.");
                }
                return false;
            }
            candidate[index] = candidate[parent] * node.localTransform;
        } else {
            candidate[index] = node.localTransform;
        }
        states[index] = kVisited;
        return true;
    };

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (!evaluate(evaluate, index)) return false;
    }
    output = std::move(candidate);
    if (error != nullptr) error->clear();
    return true;
}

bool collectVisibleGltfPrimitives(const GltfModel& model, const math::Frustum& frustum,
                                  std::vector<VisibleGltfPrimitive>& output,
                                  std::string* error) {
    std::vector<math::Mat4> worldTransforms;
    if (!model.evaluateWorldTransforms(worldTransforms, error)) return false;

    std::vector<std::uint32_t> roots;
    if (model.defaultScene >= 0) {
        const std::size_t sceneIndex = static_cast<std::size_t>(model.defaultScene);
        if (sceneIndex >= model.scenes.size()) {
            setError(error, "The glTF default scene index is out of range.");
            return false;
        }
        roots = model.scenes[sceneIndex];
    } else if (!model.scenes.empty()) {
        roots = model.scenes.front();
    } else {
        roots.reserve(model.nodes.size());
        for (std::size_t index = 0; index < model.nodes.size(); ++index) {
            if (model.nodes[index].parentIndex < 0) {
                roots.push_back(static_cast<std::uint32_t>(index));
            }
        }
    }

    std::vector<VisibleGltfPrimitive> candidate;
    std::vector<bool> visited(model.nodes.size(), false);
    const auto visit = [&](const auto& self, std::uint32_t nodeIndex) -> bool {
        const std::size_t index = static_cast<std::size_t>(nodeIndex);
        if (index >= model.nodes.size()) {
            setError(error, "A glTF scene references an out-of-range node.");
            return false;
        }
        if (visited[index]) {
            setError(error, "A glTF scene references a node more than once or contains a cycle.");
            return false;
        }
        visited[index] = true;
        const GltfNode& node = model.nodes[index];
        if (node.meshIndex >= 0) {
            const std::size_t meshIndex = static_cast<std::size_t>(node.meshIndex);
            if (meshIndex >= model.meshes.size()) {
                setError(error, "A glTF node references an out-of-range mesh.");
                return false;
            }
            const Mesh3D& mesh = model.meshes[meshIndex];
            for (std::size_t primitiveIndex = 0; primitiveIndex < mesh.primitives.size();
                 ++primitiveIndex) {
                const MeshPrimitive3D& primitive = mesh.primitives[primitiveIndex];
                if (primitive.vertices.empty() || primitive.indices.empty()) continue;
                const math::AABB worldBounds =
                    transformBounds(primitive.bounds, worldTransforms[index]);
                if (frustum.intersects(worldBounds)) {
                    candidate.push_back({nodeIndex, static_cast<std::uint32_t>(meshIndex),
                                         static_cast<std::uint32_t>(primitiveIndex),
                                         worldTransforms[index]});
                }
            }
        }
        for (const std::uint32_t child : node.children) {
            if (!self(self, child)) return false;
        }
        return true;
    };

    for (const std::uint32_t root : roots) {
        if (!visit(visit, root)) return false;
    }
    output = std::move(candidate);
    if (error != nullptr) error->clear();
    return true;
}

}  // namespace box::renderer
