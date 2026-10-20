#include "box/renderer/tilemap.hpp"

#include <algorithm>
#include <limits>

namespace box::renderer {

bool Tilemap2D::resize(std::uint32_t newWidth, std::uint32_t newHeight) {
    if (newWidth == 0U || newHeight == 0U ||
        static_cast<std::size_t>(newWidth) >
            std::numeric_limits<std::size_t>::max() / static_cast<std::size_t>(newHeight)) {
        return false;
    }
    const std::size_t cellCount = static_cast<std::size_t>(newWidth) * newHeight;
    width = newWidth;
    height = newHeight;
    tiles.assign(cellCount, kEmptyTile);
    flags.assign(cellCount, 0U);
    return true;
}

bool Tilemap2D::setTile(std::uint32_t x, std::uint32_t y,
                        std::uint32_t regionIndex, std::uint8_t tileFlags) {
    if (x >= width || y >= height || regionIndex >= regionNames.size()) return false;
    const std::size_t index = static_cast<std::size_t>(y) * width + x;
    tiles[index] = regionIndex;
    flags[index] = static_cast<std::uint8_t>(tileFlags & (kTileFlipX | kTileFlipY));
    return true;
}

void Tilemap2D::clearTile(std::uint32_t x, std::uint32_t y) noexcept {
    if (x >= width || y >= height) return;
    const std::size_t index = static_cast<std::size_t>(y) * width + x;
    tiles[index] = kEmptyTile;
    flags[index] = 0U;
}

std::uint32_t Tilemap2D::tileAt(std::uint32_t x, std::uint32_t y) const noexcept {
    if (x >= width || y >= height) return kEmptyTile;
    return tiles[static_cast<std::size_t>(y) * width + x];
}

void TilemapRenderer::draw(SpriteBatch& batch, const TextureAtlas& atlas,
                           const Tilemap2D& tilemap,
                           const math::AABB* visibleWorldBounds, math::Vec4 tint) const {
    if (atlas.texture().id() == 0 || tilemap.tileWidth <= 0.0f || tilemap.tileHeight <= 0.0f ||
        tilemap.tiles.size() != static_cast<std::size_t>(tilemap.width) * tilemap.height) {
        return;
    }

    for (std::uint32_t y = 0; y < tilemap.height; ++y) {
        const float positionY = tilemap.origin.y + static_cast<float>(y) * tilemap.tileHeight;
        for (std::uint32_t x = 0; x < tilemap.width; ++x) {
            const std::size_t cellIndex = static_cast<std::size_t>(y) * tilemap.width + x;
            const std::uint32_t regionIndex = tilemap.tiles[cellIndex];
            if (regionIndex == kEmptyTile || regionIndex >= tilemap.regionNames.size()) continue;

            const float positionX = tilemap.origin.x + static_cast<float>(x) * tilemap.tileWidth;
            if (visibleWorldBounds != nullptr &&
                (positionX > visibleWorldBounds->maximum.x ||
                 positionX + tilemap.tileWidth < visibleWorldBounds->minimum.x ||
                 positionY > visibleWorldBounds->maximum.y ||
                 positionY + tilemap.tileHeight < visibleWorldBounds->minimum.y)) {
                continue;
            }

            const AtlasRegion* region = atlas.find(tilemap.regionNames[regionIndex]);
            if (region == nullptr) continue;
            UvRect uv = region->uv;
            const std::uint8_t cellFlags = cellIndex < tilemap.flags.size() ? tilemap.flags[cellIndex] : 0U;
            if ((cellFlags & kTileFlipX) != 0U) std::swap(uv.left, uv.right);
            if ((cellFlags & kTileFlipY) != 0U) std::swap(uv.bottom, uv.top);
            batch.drawSpriteRegion(atlas.texture(), positionX, positionY,
                                   tilemap.tileWidth, tilemap.tileHeight, uv, tint);
        }
    }
}

}  // namespace box::renderer
