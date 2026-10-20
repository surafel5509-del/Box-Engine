#include "box/scene/scene_graph.hpp"

#include <algorithm>
#include <functional>

namespace box::scene {

std::uint64_t SceneGraph::key(ecs::Entity entity) noexcept {
    return (static_cast<std::uint64_t>(entity.generation) << 32U) | entity.index;
}

bool SceneGraph::addNode(ecs::Entity entity, math::Transform local,
                         std::optional<ecs::Entity> parent) {
    const std::uint64_t nodeKey = key(entity);
    if (nodes_.contains(nodeKey) || (parent.has_value() && find(*parent) == nullptr)) return false;
    SceneNode node{};
    node.entity = entity;
    node.localTransform = local;
    node.worldTransform = local.matrix();
    nodes_.emplace(nodeKey, std::move(node));
    if (parent.has_value() && !setParent(entity, parent)) {
        nodes_.erase(nodeKey);
        return false;
    }
    return true;
}

bool SceneGraph::removeNode(ecs::Entity entity) {
    SceneNode* node = find(entity);
    if (node == nullptr) return false;
    if (node->parent.has_value()) {
        if (SceneNode* parentNode = find(*node->parent)) {
            auto& children = parentNode->children;
            children.erase(std::remove(children.begin(), children.end(), entity), children.end());
        }
    }
    for (const ecs::Entity child : node->children) {
        if (SceneNode* childNode = find(child)) childNode->parent.reset();
    }
    nodes_.erase(key(entity));
    return true;
}

bool SceneGraph::setParent(ecs::Entity entity, std::optional<ecs::Entity> parent) {
    SceneNode* node = find(entity);
    if (node == nullptr) return false;
    if (parent.has_value()) {
        if (*parent == entity || find(*parent) == nullptr || isAncestor(entity, *parent)) return false;
    }

    if (node->parent == parent) return true;
    if (node->parent.has_value()) {
        if (SceneNode* oldParent = find(*node->parent)) {
            auto& oldChildren = oldParent->children;
            oldChildren.erase(std::remove(oldChildren.begin(), oldChildren.end(), entity),
                              oldChildren.end());
        }
    }
    node->parent = parent;
    if (parent.has_value()) {
        if (SceneNode* newParent = find(*parent)) newParent->children.push_back(entity);
    }
    return true;
}

bool SceneGraph::setLocalTransform(ecs::Entity entity, const math::Transform& transform) {
    SceneNode* node = find(entity);
    if (node == nullptr) return false;
    node->localTransform = transform;
    return true;
}

void SceneGraph::updateWorldTransforms() {
    std::unordered_map<std::uint64_t, std::uint8_t> visitState;
    visitState.reserve(nodes_.size());
    for (auto& [nodeKey, node] : nodes_) {
        (void)nodeKey;
        updateNode(node, visitState);
    }
}

void SceneGraph::clear() noexcept { nodes_.clear(); }

SceneNode* SceneGraph::find(ecs::Entity entity) noexcept {
    const auto found = nodes_.find(key(entity));
    return found == nodes_.end() ? nullptr : &found->second;
}

const SceneNode* SceneGraph::find(ecs::Entity entity) const noexcept {
    const auto found = nodes_.find(key(entity));
    return found == nodes_.end() ? nullptr : &found->second;
}

bool SceneGraph::isAncestor(ecs::Entity ancestor, ecs::Entity entity) const {
    const SceneNode* current = find(entity);
    while (current != nullptr && current->parent.has_value()) {
        if (*current->parent == ancestor) return true;
        current = find(*current->parent);
    }
    return false;
}

void SceneGraph::updateNode(SceneNode& node,
                            std::unordered_map<std::uint64_t, std::uint8_t>& visitState) {
    const std::uint64_t nodeKey = key(node.entity);
    const auto currentState = visitState[nodeKey];
    if (currentState == 2U) return;
    if (currentState == 1U) {
        node.worldTransform = node.localTransform.matrix();
        return;
    }
    visitState[nodeKey] = 1U;

    if (node.parent.has_value()) {
        SceneNode* parent = find(*node.parent);
        if (parent != nullptr) {
            updateNode(*parent, visitState);
            node.worldTransform = parent->worldTransform * node.localTransform.matrix();
        } else {
            node.worldTransform = node.localTransform.matrix();
        }
    } else {
        node.worldTransform = node.localTransform.matrix();
    }
    visitState[nodeKey] = 2U;
}

}  // namespace box::scene
