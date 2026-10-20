// Path: engine/src/main/cpp/include/box/ecs/ecs.hpp
#pragma once

#include <algorithm>
#include <any>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "box/core/assert.hpp"
#include "box/ecs/json.hpp"

namespace box::ecs {

using ComponentTypeId = std::uint32_t;
constexpr std::size_t kEcsChunkCapacity = 64U;

struct Entity {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    friend bool operator==(const Entity&, const Entity&) = default;
};

/** Type-erased SoA column contract used by fixed-capacity archetype chunks. */
class ComponentColumn {
public:
    virtual ~ComponentColumn() = default;
    virtual void reserve(std::size_t capacity) = 0;
    virtual void appendDefault() = 0;
    virtual void appendCopyFrom(const ComponentColumn& source, std::size_t row) = 0;
    virtual void appendAny(const std::any& value) = 0;
    virtual void assignAny(std::size_t row, const std::any& value) = 0;
    virtual void eraseSwap(std::size_t row) = 0;
    [[nodiscard]] virtual void* at(std::size_t row) = 0;
    [[nodiscard]] virtual const void* at(std::size_t row) const = 0;
    [[nodiscard]] virtual std::size_t size() const noexcept = 0;
};

template <typename T>
class TypedComponentColumn final : public ComponentColumn {
public:
    void reserve(std::size_t capacity) override { values_.reserve(capacity); }
    void appendDefault() override { values_.emplace_back(); }

    void appendCopyFrom(const ComponentColumn& source, std::size_t row) override {
        const auto* typedSource = dynamic_cast<const TypedComponentColumn<T>*>(&source);
        BOX_ASSERT(typedSource != nullptr, "Archetype column component types do not match.");
        values_.push_back(typedSource->values_.at(row));
    }

    void appendAny(const std::any& value) override { values_.push_back(std::any_cast<const T&>(value)); }
    void assignAny(std::size_t row, const std::any& value) override {
        values_.at(row) = std::any_cast<const T&>(value);
    }

    void eraseSwap(std::size_t row) override {
        if (row + 1U != values_.size()) values_[row] = std::move(values_.back());
        values_.pop_back();
    }

    [[nodiscard]] void* at(std::size_t row) override { return &values_.at(row); }
    [[nodiscard]] const void* at(std::size_t row) const override { return &values_.at(row); }
    [[nodiscard]] std::size_t size() const noexcept override { return values_.size(); }

private:
    std::vector<T> values_;
};

struct ComponentInfo {
    ComponentTypeId id;
    std::string name;
    std::type_index type;
    std::size_t size;
    std::size_t alignment;
    std::function<std::unique_ptr<ComponentColumn>()> makeColumn;
    std::function<JsonValue(const void*)> serialize;
    std::function<std::optional<std::any>(const JsonValue&)> deserialize;
};

/** Runtime component metadata used by ECS storage, JSON persistence, and inspector tools. */
class ComponentRegistry final {
public:
    using Serializer = std::function<JsonValue(const void*)>;
    using Deserializer = std::function<std::optional<std::any>(const JsonValue&)>;

    template <typename T>
    ComponentTypeId registerComponent(std::string name,
                                      std::function<JsonValue(const T&)> serializer = {},
                                      std::function<bool(const JsonValue&, T&)> deserializer = {}) {
        static_assert(std::is_default_constructible_v<T>, "ECS components must be default constructible.");
        static_assert(std::is_copy_constructible_v<T>, "ECS components must be copy constructible.");
        static_assert(std::is_move_assignable_v<T>, "ECS components must be move assignable.");

        const std::type_index key(typeid(T));
        const auto existing = typeToId_.find(key);
        if (existing != typeToId_.end()) return existing->second;
        BOX_ASSERT(!name.empty(), "Reflected component names must not be empty.");
        BOX_ASSERT(nameToId_.find(name) == nameToId_.end(),
                   "Reflected component names must be unique.");
        BOX_ASSERT(infos_.size() < std::numeric_limits<ComponentTypeId>::max(),
                   "Component type identifier capacity was exhausted.");

        const ComponentTypeId id = static_cast<ComponentTypeId>(infos_.size());
        ComponentInfo info{id, std::move(name), key, sizeof(T), alignof(T),
                           [] { return std::make_unique<TypedComponentColumn<T>>(); }, {}, {}};
        if (serializer) {
            info.serialize = [serializer = std::move(serializer)](const void* value) {
                return serializer(*static_cast<const T*>(value));
            };
        }
        if (deserializer) {
            info.deserialize = [deserializer = std::move(deserializer)](const JsonValue& value)
                -> std::optional<std::any> {
                T component{};
                if (!deserializer(value, component)) return std::nullopt;
                return std::any(std::move(component));
            };
        }

        nameToId_.emplace(info.name, id);
        typeToId_.emplace(key, id);
        infos_.push_back(std::move(info));
        return id;
    }

    template <typename T>
    ComponentTypeId ensureComponent() {
        const auto found = typeToId_.find(std::type_index(typeid(T)));
        if (found != typeToId_.end()) return found->second;
        return registerComponent<T>(typeid(T).name());
    }

    template <typename T>
    [[nodiscard]] std::optional<ComponentTypeId> findType() const {
        const auto found = typeToId_.find(std::type_index(typeid(T)));
        if (found == typeToId_.end()) return std::nullopt;
        return found->second;
    }

    [[nodiscard]] const ComponentInfo* find(ComponentTypeId id) const noexcept;
    [[nodiscard]] const ComponentInfo* findByName(const std::string& name) const noexcept;
    [[nodiscard]] const std::vector<ComponentInfo>& all() const noexcept { return infos_; }

private:
    std::vector<ComponentInfo> infos_;
    std::unordered_map<std::type_index, ComponentTypeId> typeToId_;
    std::unordered_map<std::string, ComponentTypeId> nameToId_;
};

class World;

struct ArchetypeChunk {
    explicit ArchetypeChunk(const std::vector<ComponentTypeId>& signature,
                            const ComponentRegistry& registry);

    [[nodiscard]] std::size_t size() const noexcept { return entities.size(); }
    [[nodiscard]] ComponentColumn* columnAt(std::size_t signatureIndex) noexcept;
    [[nodiscard]] const ComponentColumn* columnAt(std::size_t signatureIndex) const noexcept;

    std::vector<Entity> entities;
    std::vector<std::unique_ptr<ComponentColumn>> columns;
};

/** SoA archetype made from fixed-capacity chunks, one typed column per component type. */
class Archetype final {
public:
    Archetype(std::vector<ComponentTypeId> signature, const ComponentRegistry& registry);

    [[nodiscard]] bool has(ComponentTypeId id) const noexcept;
    [[nodiscard]] bool hasAll(const std::vector<ComponentTypeId>& ids) const noexcept;
    [[nodiscard]] const std::vector<ComponentTypeId>& signature() const noexcept { return signature_; }
    [[nodiscard]] const std::vector<std::unique_ptr<ArchetypeChunk>>& chunks() const noexcept {
        return chunks_;
    }
    [[nodiscard]] std::size_t columnIndex(ComponentTypeId id) const;
    [[nodiscard]] ComponentColumn* column(ArchetypeChunk& chunk, ComponentTypeId id) const;
    [[nodiscard]] const ComponentColumn* column(const ArchetypeChunk& chunk,
                                                ComponentTypeId id) const;
    [[nodiscard]] ArchetypeChunk& writableChunk();

private:
    friend class World;
    void eraseRow(ArchetypeChunk& chunk, std::size_t row, World& world);

    std::vector<ComponentTypeId> signature_;
    const ComponentRegistry* registry_ = nullptr;
    std::vector<std::unique_ptr<ArchetypeChunk>> chunks_;
};

/** Archetype ECS world. Structural mutation is main-thread-only; systems control their own job safety. */
class World final {
public:
    World() = default;
    World(const World&) = delete;
    World& operator=(const World&) = delete;

    [[nodiscard]] Entity create();
    [[nodiscard]] bool destroy(Entity entity);
    [[nodiscard]] bool valid(Entity entity) const noexcept;
    void clear();

    template <typename T>
    ComponentTypeId registerComponent(std::string name,
                                      std::function<JsonValue(const T&)> serializer = {},
                                      std::function<bool(const JsonValue&, T&)> deserializer = {}) {
        return registry_.registerComponent<T>(std::move(name), std::move(serializer),
                                               std::move(deserializer));
    }

    template <typename T, typename... Args>
    T* emplace(Entity entity, Args&&... args) {
        const ComponentTypeId id = registry_.ensureComponent<T>();
        T value{static_cast<Args&&>(args)...};
        if (!addValue(entity, id, std::any(std::move(value)))) return nullptr;
        return get<T>(entity);
    }

    template <typename T>
    bool remove(Entity entity) {
        const auto id = registry_.findType<T>();
        return id.has_value() && removeComponent(entity, *id);
    }

    template <typename T>
    [[nodiscard]] T* get(Entity entity) {
        if (!valid(entity)) return nullptr;
        const auto id = registry_.findType<T>();
        if (!id.has_value()) return nullptr;
        const EntityRecord& record = records_[entity.index];
        ComponentColumn* column = record.archetype->column(*record.chunk, *id);
        return column == nullptr ? nullptr : static_cast<T*>(column->at(record.row));
    }

    template <typename T>
    [[nodiscard]] const T* get(Entity entity) const {
        if (!valid(entity)) return nullptr;
        const auto id = registry_.findType<T>();
        if (!id.has_value()) return nullptr;
        const EntityRecord& record = records_[entity.index];
        const ComponentColumn* column = record.archetype->column(*record.chunk, *id);
        return column == nullptr ? nullptr : static_cast<const T*>(column->at(record.row));
    }

    template <typename T>
    [[nodiscard]] bool has(Entity entity) const { return get<T>(entity) != nullptr; }

    template <typename... Components, typename Function>
    void each(Function&& function) {
        static_assert(sizeof...(Components) > 0U, "ECS queries require at least one component type.");
        const std::array<ComponentTypeId, sizeof...(Components)> ids = {
            registry_.template ensureComponent<Components>()...};
        for (auto& [signature, archetypePointer] : archetypes_) {
            (void)signature;
            Archetype& archetype = *archetypePointer;
            bool matches = true;
            for (const ComponentTypeId id : ids) matches = matches && archetype.has(id);
            if (!matches) continue;

            for (const auto& chunkPointer : archetype.chunks()) {
                ArchetypeChunk& chunk = *chunkPointer;
                for (std::size_t row = 0; row < chunk.size(); ++row) {
                    const Entity entity = chunk.entities[row];
                    std::invoke(function, entity,
                                *static_cast<Components*>(archetype.column(
                                    chunk, registry_.template ensureComponent<Components>())->at(row))...);
                }
            }
        }
    }

    [[nodiscard]] std::vector<Entity> entities() const;
    [[nodiscard]] std::size_t entityCount() const noexcept {
        return records_.size() - freeEntityIndices_.size();
    }
    [[nodiscard]] std::vector<const ComponentInfo*> componentTypes(Entity entity) const;
    [[nodiscard]] JsonValue inspectSerializable(Entity entity) const;
    [[nodiscard]] bool addJsonComponent(Entity entity, ComponentTypeId id,
                                        const JsonValue& value);
    [[nodiscard]] bool addAnyComponent(Entity entity, ComponentTypeId id, std::any value);
    [[nodiscard]] ComponentRegistry& componentRegistry() noexcept { return registry_; }
    [[nodiscard]] const ComponentRegistry& componentRegistry() const noexcept { return registry_; }

private:
    friend class Archetype;

    struct EntityRecord {
        std::uint32_t generation = 1;
        bool alive = false;
        Archetype* archetype = nullptr;
        ArchetypeChunk* chunk = nullptr;
        std::size_t row = 0;
    };

    [[nodiscard]] Archetype& getOrCreateArchetype(std::vector<ComponentTypeId> signature);
    [[nodiscard]] bool addValue(Entity entity, ComponentTypeId id, const std::any& value);
    [[nodiscard]] bool removeComponent(Entity entity, ComponentTypeId id);
    void updateLocation(Entity entity, Archetype* archetype, ArchetypeChunk* chunk,
                        std::size_t row);

    ComponentRegistry registry_;
    std::map<std::vector<ComponentTypeId>, std::unique_ptr<Archetype>> archetypes_;
    std::vector<EntityRecord> records_;
    std::vector<std::uint32_t> freeEntityIndices_;
};

}  // namespace box::ecs
