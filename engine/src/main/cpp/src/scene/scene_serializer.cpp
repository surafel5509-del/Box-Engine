#include "box/scene/scene_serializer.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

#include "box/ecs/components.hpp"

namespace box::scene {
namespace {

using ecs::JsonValue;

[[nodiscard]] JsonValue serializeVec3(math::Vec3 value) {
    return JsonValue::Array{value.x, value.y, value.z};
}

[[nodiscard]] JsonValue serializeTransform(const math::Transform& transform) {
    return JsonValue::Object{
        {"position", serializeVec3(transform.position)},
        {"rotation", JsonValue::Array{transform.rotation.x, transform.rotation.y,
                                      transform.rotation.z, transform.rotation.w}},
        {"scale", serializeVec3(transform.scale)},
    };
}

[[nodiscard]] bool readFloat(const JsonValue& value, float& result) {
    if (!value.isNumber()) return false;
    const double number = value.asNumber();
    if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        number > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    result = static_cast<float>(number);
    return true;
}

[[nodiscard]] bool readVec3(const JsonValue* value, math::Vec3& result) {
    if (value == nullptr || value->asArray() == nullptr || value->asArray()->size() != 3U) return false;
    const auto& array = *value->asArray();
    return readFloat(array[0], result.x) && readFloat(array[1], result.y) &&
           readFloat(array[2], result.z);
}

[[nodiscard]] bool readTransform(const JsonValue& value, math::Transform& result) {
    const JsonValue* position = value.find("position");
    const JsonValue* rotation = value.find("rotation");
    const JsonValue* scale = value.find("scale");
    if (!readVec3(position, result.position) || !readVec3(scale, result.scale) ||
        rotation == nullptr || rotation->asArray() == nullptr ||
        rotation->asArray()->size() != 4U) {
        return false;
    }
    const auto& rotationArray = *rotation->asArray();
    return readFloat(rotationArray[0], result.rotation.x) &&
           readFloat(rotationArray[1], result.rotation.y) &&
           readFloat(rotationArray[2], result.rotation.z) &&
           readFloat(rotationArray[3], result.rotation.w);
}

[[nodiscard]] bool readUnsigned(const JsonValue* value, std::uint32_t& result) {
    if (value == nullptr || !value->isNumber()) return false;
    const double number = value->asNumber();
    if (!std::isfinite(number) || number < 0.0 ||
        number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()) ||
        std::floor(number) != number) {
        return false;
    }
    result = static_cast<std::uint32_t>(number);
    return true;
}

[[nodiscard]] JsonValue serializeEntityReference(ecs::Entity entity) {
    return JsonValue::Object{{"id", entity.index}, {"generation", entity.generation}};
}

[[nodiscard]] bool readEntityReference(const JsonValue& value, ecs::Entity& entity) {
    return readUnsigned(value.find("id"), entity.index) &&
           readUnsigned(value.find("generation"), entity.generation);
}

struct PreparedEntity {
    ecs::Entity source;
    std::vector<std::pair<ecs::ComponentTypeId, std::any>> components;
};

struct PreparedNode {
    ecs::Entity source;
    std::optional<ecs::Entity> parent;
    math::Transform local;
};

[[nodiscard]] bool validateHierarchy(const std::vector<PreparedNode>& nodes,
                                     const std::unordered_map<std::uint64_t, ecs::Entity>& ids) {
    std::unordered_map<std::uint64_t, std::uint8_t> states;
    std::unordered_map<std::uint64_t, std::optional<std::uint64_t>> parents;
    for (const PreparedNode& node : nodes) {
        const std::uint64_t nodeKey = SceneGraph::key(node.source);
        if (!ids.contains(nodeKey) || parents.contains(nodeKey)) return false;
        std::optional<std::uint64_t> parentKey;
        if (node.parent.has_value()) {
            const std::uint64_t value = SceneGraph::key(*node.parent);
            if (!ids.contains(value)) return false;
            parentKey = value;
        }
        parents.emplace(nodeKey, parentKey);
    }

    const auto visit = [&](const auto& self, std::uint64_t key) -> bool {
        std::uint8_t& state = states[key];
        if (state == 1U) return false;
        if (state == 2U) return true;
        state = 1U;
        const auto found = parents.find(key);
        if (found != parents.end() && found->second.has_value() &&
            !self(self, *found->second)) {
            return false;
        }
        state = 2U;
        return true;
    };
    for (const auto& [nodeKey, parent] : parents) {
        (void)parent;
        if (!visit(visit, nodeKey)) return false;
    }
    return true;
}

[[nodiscard]] bool prepareEntities(const ecs::World& world, const JsonValue& entityArray,
                                   std::vector<PreparedEntity>& prepared,
                                   std::unordered_map<std::uint64_t, ecs::Entity>& sourceIds,
                                   std::string& error) {
    const JsonValue::Array* entities = entityArray.asArray();
    if (entities == nullptr) {
        error = "Scene 'entities' must be a JSON array.";
        return false;
    }

    prepared.reserve(entities->size());
    sourceIds.reserve(entities->size());
    for (const JsonValue& item : *entities) {
        if (item.asObject() == nullptr) {
            error = "Each scene entity must be a JSON object.";
            return false;
        }
        PreparedEntity entity{};
        if (!readUnsigned(item.find("id"), entity.source.index) ||
            !readUnsigned(item.find("generation"), entity.source.generation)) {
            error = "Scene entity has an invalid id or generation.";
            return false;
        }
        const std::uint64_t sourceKey = SceneGraph::key(entity.source);
        if (!sourceIds.emplace(sourceKey, ecs::Entity{}).second) {
            error = "Scene contains duplicate entity identifiers.";
            return false;
        }
        const JsonValue* components = item.find("components");
        if (components == nullptr || components->asObject() == nullptr) {
            error = "Scene entity is missing its component object.";
            return false;
        }
        for (const auto& [name, componentValue] : *components->asObject()) {
            const ecs::ComponentInfo* info = world.componentRegistry().findByName(name);
            if (info == nullptr) continue;  // Unknown components are ignored for forward compatibility.
            if (!info->deserialize) {
                error = "Component '" + name + "' has no JSON deserializer registered.";
                return false;
            }
            std::optional<std::any> decoded = info->deserialize(componentValue);
            if (!decoded.has_value()) {
                error = "Component '" + name + "' could not be decoded.";
                return false;
            }
            entity.components.emplace_back(info->id, std::move(*decoded));
        }
        prepared.push_back(std::move(entity));
    }
    return true;
}

[[nodiscard]] bool readPrefabComponents(const ecs::World& world, const JsonValue& document,
                                       std::vector<std::pair<ecs::ComponentTypeId, std::any>>& result,
                                       std::string& error) {
    const JsonValue* components = document.find("components");
    if (components == nullptr || components->asObject() == nullptr) {
        error = "Prefab is missing its components object.";
        return false;
    }
    for (const auto& [name, componentValue] : *components->asObject()) {
        const ecs::ComponentInfo* info = world.componentRegistry().findByName(name);
        if (info == nullptr) continue;
        if (!info->deserialize) {
            error = "Component '" + name + "' has no JSON deserializer registered.";
            return false;
        }
        std::optional<std::any> decoded = info->deserialize(componentValue);
        if (!decoded.has_value()) {
            error = "Component '" + name + "' could not be decoded.";
            return false;
        }
        result.emplace_back(info->id, std::move(*decoded));
    }
    return true;
}

}  // namespace

JsonValue SceneSerializer::serialize(const ecs::World& world, const SceneGraph* graph) {
    JsonValue::Array entities;
    const std::vector<ecs::Entity> worldEntities = world.entities();
    entities.reserve(worldEntities.size());
    for (const ecs::Entity entity : worldEntities) {
        entities.emplace_back(JsonValue::Object{
            {"id", entity.index},
            {"generation", entity.generation},
            {"components", world.inspectSerializable(entity)},
        });
    }

    JsonValue::Array hierarchy;
    if (graph != nullptr) {
        std::vector<const SceneNode*> orderedNodes;
        orderedNodes.reserve(graph->nodes().size());
        for (const auto& [nodeKey, node] : graph->nodes()) {
            (void)nodeKey;
            if (world.valid(node.entity)) orderedNodes.push_back(&node);
        }
        std::sort(orderedNodes.begin(), orderedNodes.end(), [](const SceneNode* lhs,
                                                               const SceneNode* rhs) {
            if (lhs->entity.index != rhs->entity.index) return lhs->entity.index < rhs->entity.index;
            return lhs->entity.generation < rhs->entity.generation;
        });
        for (const SceneNode* node : orderedNodes) {
            hierarchy.emplace_back(JsonValue::Object{
                {"entity", serializeEntityReference(node->entity)},
                {"parent", node->parent.has_value() ? serializeEntityReference(*node->parent)
                                                     : JsonValue(nullptr)},
                {"localTransform", serializeTransform(node->localTransform)},
            });
        }
    }

    return JsonValue::Object{
        {"format", "box-engine-scene"},
        {"version", 1},
        {"entities", JsonValue(std::move(entities))},
        {"hierarchy", JsonValue(std::move(hierarchy))},
    };
}

std::string SceneSerializer::serializeString(const ecs::World& world, const SceneGraph* graph,
                                             unsigned int indentSpaces) {
    return serialize(world, graph).stringify(indentSpaces);
}

SceneLoadResult SceneSerializer::deserialize(ecs::World& world, const JsonValue& document,
                                             SceneGraph* graph) {
    SceneLoadResult result{};
    const JsonValue* format = document.find("format");
    const JsonValue* version = document.find("version");
    const std::string* formatName = format == nullptr ? nullptr : format->asString();
    if (formatName == nullptr || *formatName != "box-engine-scene" || version == nullptr ||
        version->asNumber(-1.0) != 1.0) {
        result.error = "Unsupported or malformed scene document header.";
        return result;
    }

    const JsonValue* entityArray = document.find("entities");
    if (entityArray == nullptr) {
        result.error = "Scene is missing its entities array.";
        return result;
    }
    std::vector<PreparedEntity> preparedEntities;
    std::unordered_map<std::uint64_t, ecs::Entity> sourceIds;
    if (!prepareEntities(world, *entityArray, preparedEntities, sourceIds, result.error)) return result;

    std::vector<PreparedNode> preparedNodes;
    if (graph != nullptr) {
        if (const JsonValue* hierarchy = document.find("hierarchy")) {
            const JsonValue::Array* nodeArray = hierarchy->asArray();
            if (nodeArray == nullptr) {
                result.error = "Scene 'hierarchy' must be a JSON array.";
                return result;
            }
            preparedNodes.reserve(nodeArray->size());
            for (const JsonValue& item : *nodeArray) {
                if (item.asObject() == nullptr) {
                    result.error = "Each hierarchy record must be a JSON object.";
                    return result;
                }
                PreparedNode node{};
                const JsonValue* entityValue = item.find("entity");
                const JsonValue* parentValue = item.find("parent");
                const JsonValue* transformValue = item.find("localTransform");
                if (entityValue == nullptr || !readEntityReference(*entityValue, node.source) ||
                    transformValue == nullptr || !readTransform(*transformValue, node.local)) {
                    result.error = "Hierarchy record has an invalid entity or transform.";
                    return result;
                }
                if (parentValue != nullptr && !parentValue->isNull()) {
                    ecs::Entity parent{};
                    if (!readEntityReference(*parentValue, parent)) {
                        result.error = "Hierarchy record has an invalid parent reference.";
                        return result;
                    }
                    node.parent = parent;
                }
                preparedNodes.push_back(std::move(node));
            }
            if (!validateHierarchy(preparedNodes, sourceIds)) {
                result.error = "Scene hierarchy contains duplicate, missing, or cyclic references.";
                return result;
            }
        }
    }

    world.clear();
    if (graph != nullptr) graph->clear();
    result.entities.reserve(preparedEntities.size());
    for (PreparedEntity& prepared : preparedEntities) {
        const ecs::Entity entity = world.create();
        result.entities.push_back(entity);
        result.entityRemap[SceneGraph::key(prepared.source)] = entity;
        for (auto& [componentId, component] : prepared.components) {
            if (!world.addAnyComponent(entity, componentId, std::move(component))) {
                result.error = "Decoded component could not be installed in the new scene.";
                return result;
            }
        }
    }

    if (graph != nullptr) {
        for (const PreparedNode& prepared : preparedNodes) {
            const auto found = result.entityRemap.find(SceneGraph::key(prepared.source));
            if (found != result.entityRemap.end()) (void)graph->addNode(found->second, prepared.local);
        }
        for (const PreparedNode& prepared : preparedNodes) {
            if (!prepared.parent.has_value()) continue;
            const auto child = result.entityRemap.find(SceneGraph::key(prepared.source));
            const auto parent = result.entityRemap.find(SceneGraph::key(*prepared.parent));
            if (child == result.entityRemap.end() || parent == result.entityRemap.end() ||
                !graph->setParent(child->second, parent->second)) {
                result.error = "Validated hierarchy could not be installed.";
                return result;
            }
        }
        graph->updateWorldTransforms();
    }

    result.success = true;
    return result;
}

SceneLoadResult SceneSerializer::deserializeString(ecs::World& world, const std::string& source,
                                                   SceneGraph* graph) {
    const ecs::JsonValue::ParseResult parsed = ecs::JsonValue::parse(source);
    if (!parsed.succeeded()) {
        SceneLoadResult result{};
        result.error = "JSON parse error at byte " + std::to_string(parsed.errorOffset) +
                       ": " + parsed.error;
        return result;
    }
    return deserialize(world, parsed.value, graph);
}

JsonValue SceneSerializer::makePrefab(const ecs::World& world, ecs::Entity entity) {
    if (!world.valid(entity)) return JsonValue(nullptr);
    return JsonValue::Object{
        {"format", "box-engine-prefab"},
        {"version", 1},
        {"components", world.inspectSerializable(entity)},
    };
}

ecs::Entity SceneSerializer::instantiatePrefab(ecs::World& world, const JsonValue& document,
                                               std::string* error) {
    const JsonValue* format = document.find("format");
    const JsonValue* version = document.find("version");
    const std::string* formatName = format == nullptr ? nullptr : format->asString();
    if (formatName == nullptr || *formatName != "box-engine-prefab" || version == nullptr ||
        version->asNumber(-1.0) != 1.0) {
        if (error != nullptr) *error = "Unsupported or malformed prefab document header.";
        return {};
    }

    std::vector<std::pair<ecs::ComponentTypeId, std::any>> components;
    std::string decodeError;
    if (!readPrefabComponents(world, document, components, decodeError)) {
        if (error != nullptr) *error = std::move(decodeError);
        return {};
    }

    const ecs::Entity entity = world.create();
    for (auto& [componentId, component] : components) {
        if (!world.addAnyComponent(entity, componentId, std::move(component))) {
            (void)world.destroy(entity);
            if (error != nullptr) *error = "Decoded prefab component could not be installed.";
            return {};
        }
    }
    return entity;
}

void registerCoreComponents(ecs::ComponentRegistry& registry) {
    registry.registerComponent<ecs::NameComponent>(
        "Name",
        [](const ecs::NameComponent& component) { return JsonValue(component.value); },
        [](const JsonValue& value, ecs::NameComponent& component) {
            const std::string* name = value.asString();
            if (name == nullptr) return false;
            component.value = *name;
            return true;
        });

    registry.registerComponent<math::Transform>(
        "Transform",
        [](const math::Transform& transform) { return serializeTransform(transform); },
        [](const JsonValue& value, math::Transform& transform) {
            return readTransform(value, transform);
        });
}

}  // namespace box::scene
