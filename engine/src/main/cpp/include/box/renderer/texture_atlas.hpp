#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "box/renderer/sprite_batch.hpp"

namespace box::renderer {

/** Atlas UV fields use OpenGL coordinates in left, bottom, right, top order. */
struct AtlasRegion {
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    UvRect uv{};
};

/** RGBA8 atlas input rows are ordered top-to-bottom, as in decoded PNG images. */
struct AtlasImage {
    std::string name;
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgbaPixels;
};

/** One runtime atlas page and its normalized UV metadata. */
class TextureAtlas final {
public:
    [[nodiscard]] bool upload(int width, int height, const std::uint8_t* rgbaPixels,
                              std::vector<AtlasRegion> regions);
    void destroy() noexcept;
    void abandon() noexcept;
    [[nodiscard]] const AtlasRegion* find(const std::string& name) const noexcept;
    [[nodiscard]] const Texture2D& texture() const noexcept { return texture_; }
    [[nodiscard]] Texture2D& texture() noexcept { return texture_; }
    [[nodiscard]] int width() const noexcept { return width_; }
    [[nodiscard]] int height() const noexcept { return height_; }
    [[nodiscard]] std::size_t regionCount() const noexcept { return regions_.size(); }

private:
    Texture2D texture_;
    std::unordered_map<std::string, AtlasRegion> regions_;
    int width_ = 0;
    int height_ = 0;
};

/** CPU-side shelf packer for runtime atlas generation; upload/build must run on the GL thread. */
class TextureAtlasBuilder final {
public:
    explicit TextureAtlasBuilder(int maximumSize = 2048, int padding = 2);

    [[nodiscard]] bool add(AtlasImage image);
    void clear() noexcept { images_.clear(); }
    [[nodiscard]] bool build(TextureAtlas& output, std::string* error = nullptr) const;
    [[nodiscard]] std::size_t imageCount() const noexcept { return images_.size(); }

private:
    int maximumSize_;
    int padding_;
    std::vector<AtlasImage> images_;
};

}  // namespace box::renderer
