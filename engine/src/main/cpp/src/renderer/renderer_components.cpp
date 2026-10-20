#include "box/renderer/renderer_components.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace box::renderer {
namespace {

using ecs::JsonValue;

[[nodiscard]] JsonValue vec2Json(math::Vec2 value) { return JsonValue::Array{value.x, value.y}; }
[[nodiscard]] JsonValue vec4Json(math::Vec4 value) {
    return JsonValue::Array{value.x, value.y, value.z, value.w};
}

[[nodiscard]] bool readFloat(const JsonValue* value, float& output) {
    if (value == nullptr || !value->isNumber()) return false;
    const double number = value->asNumber();
    if (!std::isfinite(number) || number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        number > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    output = static_cast<float>(number);
    return true;
}

[[nodiscard]] bool readDouble(const JsonValue* value, double& output) {
    if (value == nullptr || !value->isNumber()) return false;
    output = value->asNumber();
    return std::isfinite(output);
}

[[nodiscard]] bool readInt32(const JsonValue* value, std::int32_t& output) {
    if (value == nullptr || !value->isNumber()) return false;
    const double number = value->asNumber();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < std::numeric_limits<std::int32_t>::min() ||
        number > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    output = static_cast<std::int32_t>(number);
    return true;
}

[[nodiscard]] bool readUnsigned(const JsonValue* value, std::uint32_t& output) {
    if (value == nullptr || !value->isNumber()) return false;
    const double number = value->asNumber();
    if (!std::isfinite(number) || std::floor(number) != number || number < 0.0 ||
        number > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    output = static_cast<std::uint32_t>(number);
    return true;
}

[[nodiscard]] bool readBool(const JsonValue* value, bool& output) {
    if (value == nullptr || !value->isBool()) return false;
    output = value->asBool();
    return true;
}

[[nodiscard]] bool readString(const JsonValue* value, std::string& output) {
    if (value == nullptr || value->asString() == nullptr) return false;
    output = *value->asString();
    return true;
}

[[nodiscard]] bool readVec2(const JsonValue* value, math::Vec2& output) {
    if (value == nullptr || value->asArray() == nullptr || value->asArray()->size() != 2U) return false;
    const auto& array = *value->asArray();
    return readFloat(&array[0], output.x) && readFloat(&array[1], output.y);
}

[[nodiscard]] bool readVec4(const JsonValue* value, math::Vec4& output) {
    if (value == nullptr || value->asArray() == nullptr || value->asArray()->size() != 4U) return false;
    const auto& array = *value->asArray();
    return readFloat(&array[0], output.x) && readFloat(&array[1], output.y) &&
           readFloat(&array[2], output.z) && readFloat(&array[3], output.w);
}

[[nodiscard]] JsonValue cameraJson(const Camera2D& camera) {
    return JsonValue::Object{
        {"position", vec2Json(camera.position)},
        {"zoom", camera.zoom},
        {"viewport", JsonValue::Array{camera.viewport.x, camera.viewport.y,
                                      camera.viewport.width, camera.viewport.height}},
        {"pixelPerfect", camera.pixelPerfect},
        {"pixelsPerUnit", camera.pixelsPerUnit},
    };
}

[[nodiscard]] bool readCamera(const JsonValue& value, Camera2D& camera) {
    const JsonValue* viewport = value.find("viewport");
    if (viewport == nullptr || viewport->asArray() == nullptr || viewport->asArray()->size() != 4U ||
        !readVec2(value.find("position"), camera.position) ||
        !readFloat(value.find("zoom"), camera.zoom) ||
        !readBool(value.find("pixelPerfect"), camera.pixelPerfect) ||
        !readFloat(value.find("pixelsPerUnit"), camera.pixelsPerUnit)) {
        return false;
    }
    const auto& values = *viewport->asArray();
    ViewportRect rectangle{};
    if (!readFloat(&values[0], rectangle.x) || !readFloat(&values[1], rectangle.y) ||
        !readFloat(&values[2], rectangle.width) || !readFloat(&values[3], rectangle.height)) {
        return false;
    }
    camera.setViewport(rectangle);
    camera.setZoom(camera.zoom);
    camera.setPixelPerfect(camera.pixelPerfect, camera.pixelsPerUnit);
    return true;
}

[[nodiscard]] JsonValue tilemapJson(const Tilemap2D& tilemap) {
    JsonValue::Array regions;
    regions.reserve(tilemap.regionNames.size());
    for (const std::string& name : tilemap.regionNames) regions.emplace_back(name);
    JsonValue::Array tiles;
    tiles.reserve(tilemap.tiles.size());
    for (const std::uint32_t tile : tilemap.tiles) tiles.emplace_back(tile);
    JsonValue::Array flags;
    flags.reserve(tilemap.flags.size());
    for (const std::uint8_t flag : tilemap.flags) flags.emplace_back(static_cast<int>(flag));
    return JsonValue::Object{
        {"width", tilemap.width},
        {"height", tilemap.height},
        {"tileWidth", tilemap.tileWidth},
        {"tileHeight", tilemap.tileHeight},
        {"origin", vec2Json(tilemap.origin)},
        {"regions", JsonValue(std::move(regions))},
        {"tiles", JsonValue(std::move(tiles))},
        {"flags", JsonValue(std::move(flags))},
    };
}

[[nodiscard]] bool readTilemap(const JsonValue& value, Tilemap2D& tilemap) {
    constexpr std::size_t kMaximumTileCount = 4U * 1024U * 1024U;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    const JsonValue* regions = value.find("regions");
    const JsonValue* tiles = value.find("tiles");
    const JsonValue* flags = value.find("flags");
    if (!readUnsigned(value.find("width"), width) || !readUnsigned(value.find("height"), height) ||
        width == 0U || height == 0U || static_cast<std::size_t>(width) > kMaximumTileCount / height ||
        regions == nullptr || regions->asArray() == nullptr || regions->asArray()->size() > 65536U ||
        tiles == nullptr || tiles->asArray() == nullptr ||
        tiles->asArray()->size() != static_cast<std::size_t>(width) * height ||
        (flags != nullptr && (flags->asArray() == nullptr ||
                              flags->asArray()->size() != tiles->asArray()->size())) ||
        !readFloat(value.find("tileWidth"), tilemap.tileWidth) ||
        !readFloat(value.find("tileHeight"), tilemap.tileHeight) ||
        tilemap.tileWidth <= 0.0f || tilemap.tileHeight <= 0.0f ||
        !readVec2(value.find("origin"), tilemap.origin)) {
        return false;
    }

    tilemap.regionNames.clear();
    tilemap.regionNames.reserve(regions->asArray()->size());
    for (const JsonValue& region : *regions->asArray()) {
        if (region.asString() == nullptr) return false;
        tilemap.regionNames.push_back(*region.asString());
    }
    if (!tilemap.resize(width, height)) return false;
    for (std::size_t index = 0; index < tiles->asArray()->size(); ++index) {
        if (!readUnsigned(&(*tiles->asArray())[index], tilemap.tiles[index])) return false;
        if (tilemap.tiles[index] != kEmptyTile && tilemap.tiles[index] >= tilemap.regionNames.size()) {
            return false;
        }
        if (flags != nullptr) {
            std::uint32_t parsedFlag = 0;
            if (!readUnsigned(&(*flags->asArray())[index], parsedFlag) || parsedFlag > 255U) return false;
            tilemap.flags[index] = static_cast<std::uint8_t>(parsedFlag);
        }
    }
    return true;
}

[[nodiscard]] JsonValue particleConfigJson(const ParticleEmitterConfig& config) {
    return JsonValue::Object{
        {"emissionRate", config.emissionRate},
        {"lifetimeSeconds", config.lifetimeSeconds},
        {"speed", config.speed},
        {"spreadRadians", config.spreadRadians},
        {"direction", vec2Json(config.direction)},
        {"gravity", vec2Json(config.gravity)},
        {"startSize", config.startSize},
        {"endSize", config.endSize},
        {"startColor", vec4Json(config.startColor)},
        {"endColor", vec4Json(config.endColor)},
    };
}

[[nodiscard]] bool readParticleConfig(const JsonValue& value, ParticleEmitterConfig& config) {
    return readFloat(value.find("emissionRate"), config.emissionRate) &&
           readFloat(value.find("lifetimeSeconds"), config.lifetimeSeconds) &&
           readFloat(value.find("speed"), config.speed) &&
           readFloat(value.find("spreadRadians"), config.spreadRadians) &&
           readVec2(value.find("direction"), config.direction) &&
           readVec2(value.find("gravity"), config.gravity) &&
           readFloat(value.find("startSize"), config.startSize) &&
           readFloat(value.find("endSize"), config.endSize) &&
           readVec4(value.find("startColor"), config.startColor) &&
           readVec4(value.find("endColor"), config.endColor);
}

}  // namespace

void registerRendererComponents(ecs::ComponentRegistry& registry) {
    registry.registerComponent<SpriteComponent>(
        "Sprite",
        [](const SpriteComponent& sprite) {
            return JsonValue::Object{
                {"textureAsset", sprite.textureAsset},
                {"regionName", sprite.regionName},
                {"size", vec2Json(sprite.size)},
                {"tint", vec4Json(sprite.tint)},
                {"layer", sprite.layer},
                {"visible", sprite.visible},
            };
        },
        [](const JsonValue& value, SpriteComponent& sprite) {
            return readString(value.find("textureAsset"), sprite.textureAsset) &&
                   readString(value.find("regionName"), sprite.regionName) &&
                   readVec2(value.find("size"), sprite.size) &&
                   readVec4(value.find("tint"), sprite.tint) &&
                   readInt32(value.find("layer"), sprite.layer) &&
                   readBool(value.find("visible"), sprite.visible);
        });

    registry.registerComponent<SpriteAnimationComponent>(
        "SpriteAnimation",
        [](const SpriteAnimationComponent& animation) {
            return JsonValue::Object{
                {"clipName", animation.clipName},
                {"elapsedSeconds", animation.elapsedSeconds},
                {"playbackRate", animation.playbackRate},
                {"blendSeconds", animation.blendSeconds},
                {"looping", animation.looping},
                {"playing", animation.playing},
            };
        },
        [](const JsonValue& value, SpriteAnimationComponent& animation) {
            return readString(value.find("clipName"), animation.clipName) &&
                   readDouble(value.find("elapsedSeconds"), animation.elapsedSeconds) &&
                   readFloat(value.find("playbackRate"), animation.playbackRate) &&
                   readFloat(value.find("blendSeconds"), animation.blendSeconds) &&
                   readBool(value.find("looping"), animation.looping) &&
                   readBool(value.find("playing"), animation.playing);
        });

    registry.registerComponent<Camera2D>(
        "Camera2D", [](const Camera2D& camera) { return cameraJson(camera); },
        [](const JsonValue& value, Camera2D& camera) { return readCamera(value, camera); });

    registry.registerComponent<Tilemap2D>(
        "Tilemap2D", [](const Tilemap2D& tilemap) { return tilemapJson(tilemap); },
        [](const JsonValue& value, Tilemap2D& tilemap) { return readTilemap(value, tilemap); });

    registry.registerComponent<SdfTextComponent>(
        "SdfText",
        [](const SdfTextComponent& text) {
            return JsonValue::Object{
                {"text", text.text},
                {"fontAsset", text.fontAsset},
                {"color", vec4Json(text.color)},
                {"scale", text.scale},
                {"layer", text.layer},
            };
        },
        [](const JsonValue& value, SdfTextComponent& text) {
            return readString(value.find("text"), text.text) &&
                   readString(value.find("fontAsset"), text.fontAsset) &&
                   readVec4(value.find("color"), text.color) &&
                   readFloat(value.find("scale"), text.scale) &&
                   readInt32(value.find("layer"), text.layer);
        });

    registry.registerComponent<ParticleEmitterComponent>(
        "ParticleEmitter",
        [](const ParticleEmitterComponent& emitter) {
            return JsonValue::Object{
                {"config", particleConfigJson(emitter.config)},
                {"enabled", emitter.enabled},
            };
        },
        [](const JsonValue& value, ParticleEmitterComponent& emitter) {
            const JsonValue* config = value.find("config");
            return config != nullptr && readParticleConfig(*config, emitter.config) &&
                   readBool(value.find("enabled"), emitter.enabled);
        });
}

}  // namespace box::renderer
