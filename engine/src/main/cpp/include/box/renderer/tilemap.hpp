#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "box/math/math.hpp"
#include "box/renderer/sprite_batch.hpp"
#include "box/renderer/texture_atlas.hpp"

namespace box::renderer {

constexpr std::uint32_t kEmptyTile = 0xFFFFFFFFU;
constexpr std::uint8_t kTileFlipX = 0x01U;
constexpr std::uint8_t kTileFlipY = 0x02U;

/** CPU-side tile grid; region indices address names in the associated atlas. */
struct Tilemap2D {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float tileWidth = 16.0f;
    float tileHeight = 16.0f;
    math::Vec2 origin{};
    std::vector<std::string> regionNames;
    std::vector<std::uint32_t> tiles;
    std::vector<std::uint8_t> flags;

    [[nodiscard]] bool resize(std::uint32_t newWidth, std::uint32_t newHeight);
    [[nodiscard]] bool setTile(std::uint32_t x, std::uint32_t y,
                               std::uint32_t regionIndex, std::uint8_t tileFlags = 0U);
    void clearTile(std::uint32_t x, std::uint32_t y) noexcept;
    [[nodiscard]] std::uint32_t tileAt(std::uint32_t x, std::uint32_t y) const noexcept;
};

/** Draws visible tile cells through the shared sprite batch and a single atlas page. */
class TilemapRenderer final {
public:
    void draw(SpriteBatch& batch, const TextureAtlas& atlas, const Tilemap2D& tilemap,
              const math::AABB* visibleWorldBounds = nullptr,
              math::Vec4 tint = {1.0f, 1.0f, 1.0f, 1.0f}) const;
};

}  // namespace box::renderer
