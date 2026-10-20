#include "box/ecs/ecs.hpp"

#include <limits>

namespace box::ecs {

const ComponentInfo* ComponentRegistry::find(ComponentTypeId id) const noexcept {
    return id < infos_.size() ? &infos_[id] : nullptr;
}

const ComponentInfo* ComponentRegistry::findByName(const std::string& name) const noexcept {
    const auto found = nameToId_.find(name);
    return found == nameToId_.end() ? nullptr : find(found->second);
}

ArchetypeChunk::ArchetypeChunk(const std::vector<ComponentTypeId>& signature,
                               const ComponentRegistry& registry) {
    entities.reserve(kEcsChunkCapacity);
    columns.reserve(signature.size());
    for (const ComponentTypeId id : signature) {
        const ComponentInfo* info = registry.find(id);
        BOX_ASSERT(info != nullptr, "Cannot create a chunk for an unregistered component type.");
        columns.push_back(info->makeColumn());
        columns.back()->reserve(kEcsChunkCapacity);
    }
}

ComponentColumn* ArchetypeChunk::columnAt(std::size_t signatureIndex) noexcept {
    return signatureIndex < columns.size() ? columns[signatureIndex].get() : nullptr;
}

const ComponentColumn* ArchetypeChunk::columnAt(std::size_t signatureIndex) const noexcept {
    return signatureIndex < columns.size() ? columns[signatureIndex].get() : nullptr;
}

Archetype::Archetype(std::vector<ComponentTypeId> signature,
                     const ComponentRegistry& registry)
    : signature_(std::move(signature)), registry_(&registry) {}

bool Archetype::has(ComponentTypeId id) const noexcept {
    return std::binary_search(signature_.begin(), signature_.end(), id);
}

bool Archetype::hasAll(const std::vector<ComponentTypeId>& ids) const noexcept {
    return std::all_of(ids.begin(), ids.end(), [this](ComponentTypeId id) { return has(id); });
}

std::size_t Archetype::columnIndex(ComponentTypeId id) const {
    const auto found = std::lower_bound(signature_.begin(), signature_.end(), id);
    BOX_ASSERT(found != signature_.end() && *found == id,
               "Requested component is not present in this archetype.");
    return static_cast<std::size_t>(found - signature_.begin());
}

ComponentColumn* Archetype::column(ArchetypeChunk& chunk, ComponentTypeId id) const {
    const std::size_t index = columnIndex(id);
    return chunk.columnAt(index);
}

const ComponentColumn* Archetype::column(const ArchetypeChunk& chunk,
                                         ComponentTypeId id) const {
    const std::size_t index = columnIndex(id);
    return chunk.columnAt(index);
}

ArchetypeChunk& Archetype::writableChunk() {
    if (chunks_.empty() || chunks_.back()->size() >= kEcsChunkCapacity) {
        chunks_.push_back(std::make_unique<ArchetypeChunk>(signature_, *registry_));
    }
    return *chunks_.back();
}

void Archetype::eraseRow(ArchetypeChunk& chunk, std::size_t row, World& world) {
    BOX_ASSERT(row < chunk.size(), "Archetype row is outside the chunk.");
    const std::size_t lastRow = chunk.size() - 1U;
    const Entity movedEntity = chunk.entities[lastRow];
    for (const auto& columnValue : chunk.columns) columnValue->eraseSwap(row);
    if (row != lastRow) {
        chunk.entities[row] = movedEntity;
        world.updateLocation(movedEntity, this, &chunk, row);
    }
    chunk.entities.pop_back();
}

Entity World::create() {
    std::uint32_t index = 0;
    if (freeEntityIndices_.empty()) {
        BOX_ASSERT(records_.size() < std::numeric_limits<std::uint32_t>::max(),
                   "Entity index capacity was exhausted.");
        index = static_cast<std::uint32_t>(records_.size());
        records_.push_back(EntityRecord{});
    } else {
        index = freeEntityIndices_.back();
        freeEntityIndices_.pop_back();
    }

    EntityRecord& record = records_[index];
    record.alive = true;
    const Entity entity{index, record.generation};
    Archetype& archetype = getOrCreateArchetype({});
    ArchetypeChunk& chunk = archetype.writableChunk();
    const std::size_t row = chunk.size();
    chunk.entities.push_back(entity);
    record.archetype = &archetype;
    record.chunk = &chunk;
    record.row = row;
    return entity;
}

bool World::destroy(Entity entity) {
    if (!valid(entity)) return false;
    EntityRecord& record = records_[entity.index];
    record.archetype->eraseRow(*record.chunk, record.row, *this);
    record.alive = false;
    record.archetype = nullptr;
    record.chunk = nullptr;
    record.row = 0;
    ++record.generation;
    if (record.generation == 0U) ++record.generation;
    freeEntityIndices_.push_back(entity.index);
    return true;
}

bool World::valid(Entity entity) const noexcept {
    return entity.index < records_.size() && records_[entity.index].alive &&
           records_[entity.index].generation == entity.generation;
}

void World::clear() {
    freeEntityIndices_.clear();
    for (std::size_t index = 0; index < records_.size(); ++index) {
        EntityRecord& record = records_[index];
        ++record.generation;
        if (record.generation == 0U) ++record.generation;
        record.alive = false;
        record.archetype = nullptr;
        record.chunk = nullptr;
        record.row = 0;
        freeEntityIndices_.push_back(static_cast<std::uint32_t>(index));
    }
    archetypes_.clear();
}

std::vector<Entity> World::entities() const {
    std::vector<Entity> result;
    result.reserve(records_.size() - freeEntityIndices_.size());
    for (std::size_t index = 0; index < records_.size(); ++index) {
        const EntityRecord& record = records_[index];
        if (record.alive) result.push_back({static_cast<std::uint32_t>(index), record.generation});
    }
    return result;
}

std::vector<const ComponentInfo*> World::componentTypes(Entity entity) const {
    std::vector<const ComponentInfo*> result;
    if (!valid(entity)) return result;
    const EntityRecord& record = records_[entity.index];
    result.reserve(record.archetype->signature().size());
    for (const ComponentTypeId id : record.archetype->signature()) {
        if (const ComponentInfo* info = registry_.find(id)) result.push_back(info);
    }
    return result;
}

JsonValue World::inspectSerializable(Entity entity) const {
    JsonValue::Object components;
    if (!valid(entity)) return JsonValue(std::move(components));
    const EntityRecord& record = records_[entity.index];
    const auto& signature = record.archetype->signature();
    for (std::size_t index = 0; index < signature.size(); ++index) {
        const ComponentInfo* info = registry_.find(signature[index]);
        if (info == nullptr || !info->serialize) continue;
        const ComponentColumn* column = record.chunk->columnAt(index);
        components.emplace(info->name, info->serialize(column->at(record.row)));
    }
    return JsonValue(std::move(components));
}

bool World::addJsonComponent(Entity entity, ComponentTypeId id, const JsonValue& value) {
    if (!valid(entity)) return false;
    const ComponentInfo* info = registry_.find(id);
    if (info == nullptr || !info->deserialize) return false;
    std::optional<std::any> component = info->deserialize(value);
    if (!component.has_value()) return false;
    return addValue(entity, id, *component);
}

bool World::addAnyComponent(Entity entity, ComponentTypeId id, std::any value) {
    return addValue(entity, id, value);
}

Archetype& World::getOrCreateArchetype(std::vector<ComponentTypeId> signature) {
    std::sort(signature.begin(), signature.end());
    signature.erase(std::unique(signature.begin(), signature.end()), signature.end());
    auto found = archetypes_.find(signature);
    if (found != archetypes_.end()) return *found->second;

    auto archetype = std::make_unique<Archetype>(signature, registry_);
    Archetype& result = *archetype;
    archetypes_.emplace(std::move(signature), std::move(archetype));
    return result;
}

bool World::addValue(Entity entity, ComponentTypeId id, const std::any& value) {
    if (!valid(entity)) return false;
    const ComponentInfo* info = registry_.find(id);
    if (info == nullptr || value.type() != info->type) return false;

    EntityRecord& record = records_[entity.index];
    Archetype* sourceArchetype = record.archetype;
    ArchetypeChunk* sourceChunk = record.chunk;
    const std::size_t sourceRow = record.row;
    if (sourceArchetype->has(id)) {
        sourceArchetype->column(*sourceChunk, id)->assignAny(sourceRow, value);
        return true;
    }

    std::vector<ComponentTypeId> destinationSignature = sourceArchetype->signature();
    destinationSignature.insert(std::lower_bound(destinationSignature.begin(),
                                                   destinationSignature.end(), id), id);
    Archetype& destination = getOrCreateArchetype(std::move(destinationSignature));
    ArchetypeChunk& destinationChunk = destination.writableChunk();
    const std::size_t destinationRow = destinationChunk.size();
    for (std::size_t index = 0; index < destination.signature().size(); ++index) {
        const ComponentTypeId componentId = destination.signature()[index];
        ComponentColumn* destinationColumn = destinationChunk.columnAt(index);
        if (componentId == id) {
            destinationColumn->appendAny(value);
        } else {
            const ComponentColumn* sourceColumn = sourceArchetype->column(*sourceChunk, componentId);
            destinationColumn->appendCopyFrom(*sourceColumn, sourceRow);
        }
    }
    destinationChunk.entities.push_back(entity);
    updateLocation(entity, &destination, &destinationChunk, destinationRow);
    sourceArchetype->eraseRow(*sourceChunk, sourceRow, *this);
    return true;
}

bool World::removeComponent(Entity entity, ComponentTypeId id) {
    if (!valid(entity)) return false;
    EntityRecord& record = records_[entity.index];
    Archetype* sourceArchetype = record.archetype;
    if (!sourceArchetype->has(id)) return false;

    ArchetypeChunk* sourceChunk = record.chunk;
    const std::size_t sourceRow = record.row;
    std::vector<ComponentTypeId> destinationSignature = sourceArchetype->signature();
    destinationSignature.erase(std::lower_bound(destinationSignature.begin(),
                                                  destinationSignature.end(), id));
    Archetype& destination = getOrCreateArchetype(std::move(destinationSignature));
    ArchetypeChunk& destinationChunk = destination.writableChunk();
    const std::size_t destinationRow = destinationChunk.size();
    for (std::size_t index = 0; index < destination.signature().size(); ++index) {
        const ComponentTypeId componentId = destination.signature()[index];
        const ComponentColumn* sourceColumn = sourceArchetype->column(*sourceChunk, componentId);
        destinationChunk.columnAt(index)->appendCopyFrom(*sourceColumn, sourceRow);
    }
    destinationChunk.entities.push_back(entity);
    updateLocation(entity, &destination, &destinationChunk, destinationRow);
    sourceArchetype->eraseRow(*sourceChunk, sourceRow, *this);
    return true;
}

void World::updateLocation(Entity entity, Archetype* archetype, ArchetypeChunk* chunk,
                           std::size_t row) {
    BOX_ASSERT(entity.index < records_.size(), "Entity location update uses an invalid index.");
    EntityRecord& record = records_[entity.index];
    BOX_ASSERT(record.alive && record.generation == entity.generation,
               "Entity location update uses a stale entity handle.");
    record.archetype = archetype;
    record.chunk = chunk;
    record.row = row;
}

}  // namespace box::ecs
