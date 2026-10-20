#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "box/ecs/ecs.hpp"
#include "box/math/math.hpp"

namespace box::scene {

struct SceneNode {
    ecs::Entity entity{};
    std::optional<ecs::Entity> parent;
    std::vector<ecs::Entity> children;
    math::Transform localTransform{};
    math::Mat4 worldTransform = math::Mat4::identity();
};

/** Hierarchical transform graph with cycle-safe reparenting and cached world matrices. */
class SceneGraph final {
public:
    [[nodiscard]] bool addNode(ecs::Entity entity, math::Transform local = {},
                               std::optional<ecs::Entity> parent = std::nullopt);
    [[nodiscard]] bool removeNode(ecs::Entity entity);
    [[nodiscard]] bool setParent(ecs::Entity entity, std::optional<ecs::Entity> parent);
    [[nodiscard]] bool setLocalTransform(ecs::Entity entity, const math::Transform& transform);
    void updateWorldTransforms();
    void clear() noexcept;

    [[nodiscard]] SceneNode* find(ecs::Entity entity) noexcept;
    [[nodiscard]] const SceneNode* find(ecs::Entity entity) const noexcept;
    [[nodiscard]] const std::unordered_map<std::uint64_t, SceneNode>& nodes() const noexcept {
        return nodes_;
    }

    [[nodiscard]] static std::uint64_t key(ecs::Entity entity) noexcept;

private:
    [[nodiscard]] bool isAncestor(ecs::Entity ancestor, ecs::Entity entity) const;
    void updateNode(SceneNode& node, std::unordered_map<std::uint64_t, std::uint8_t>& visitState);

    std::unordered_map<std::uint64_t, SceneNode> nodes_;
};

}  // namespace box::scene
