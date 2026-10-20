#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "box/ecs/ecs.hpp"
#include "box/scene/scene_graph.hpp"

namespace box::scene {

struct SceneLoadResult {
    bool success = false;
    std::string error;
    std::vector<ecs::Entity> entities;
    std::unordered_map<std::uint64_t, ecs::Entity> entityRemap;
};

/** Versioned scene/prefab JSON serializer backed by component reflection metadata. */
class SceneSerializer final {
public:
    [[nodiscard]] static ecs::JsonValue serialize(const ecs::World& world,
                                                  const SceneGraph* graph = nullptr);
    [[nodiscard]] static std::string serializeString(const ecs::World& world,
                                                     const SceneGraph* graph = nullptr,
                                                     unsigned int indentSpaces = 2U);
    [[nodiscard]] static SceneLoadResult deserialize(ecs::World& world,
                                                     const ecs::JsonValue& document,
                                                     SceneGraph* graph = nullptr);
    [[nodiscard]] static SceneLoadResult deserializeString(ecs::World& world,
                                                           const std::string& source,
                                                           SceneGraph* graph = nullptr);

    [[nodiscard]] static ecs::JsonValue makePrefab(const ecs::World& world, ecs::Entity entity);
    [[nodiscard]] static ecs::Entity instantiatePrefab(ecs::World& world,
                                                       const ecs::JsonValue& document,
                                                       std::string* error = nullptr);
};

/** Registers serializable engine-owned core component types. */
void registerCoreComponents(ecs::ComponentRegistry& registry);

}  // namespace box::scene
