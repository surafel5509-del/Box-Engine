#include "box/renderer/texture_atlas.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace box::renderer {
namespace {

struct Placement {
    std::size_t imageIndex = 0;
    int x = 0;
    int y = 0;
};

void setError(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
}

}  // namespace

bool TextureAtlas::upload(int width, int height, const std::uint8_t* rgbaPixels,
                          std::vector<AtlasRegion> regions) {
    if (width <= 0 || height <= 0 || rgbaPixels == nullptr) return false;
    if (!texture_.uploadRgba8(width, height, rgbaPixels, GL_LINEAR, GL_LINEAR)) return false;
    regions_.clear();
    regions_.reserve(regions.size());
    for (AtlasRegion& region : regions) regions_.insert_or_assign(region.name, std::move(region));
    width_ = width;
    height_ = height;
    return true;
}

void TextureAtlas::destroy() noexcept {
    texture_.destroy();
    regions_.clear();
    width_ = 0;
    height_ = 0;
}

void TextureAtlas::abandon() noexcept {
    texture_.abandon();
    regions_.clear();
    width_ = 0;
    height_ = 0;
}

const AtlasRegion* TextureAtlas::find(const std::string& name) const noexcept {
    const auto found = regions_.find(name);
    return found == regions_.end() ? nullptr : &found->second;
}

TextureAtlasBuilder::TextureAtlasBuilder(int maximumSize, int padding)
    : maximumSize_(std::clamp(maximumSize, 16, 8192)),
      padding_(std::clamp(padding, 0, 16)) {}

bool TextureAtlasBuilder::add(AtlasImage image) {
    if (image.name.empty() || image.width <= 0 || image.height <= 0) return false;
    const std::size_t width = static_cast<std::size_t>(image.width);
    const std::size_t height = static_cast<std::size_t>(image.height);
    if (width > std::numeric_limits<std::size_t>::max() / height / 4U ||
        image.rgbaPixels.size() != width * height * 4U) {
        return false;
    }
    const bool duplicate = std::any_of(images_.begin(), images_.end(), [&image](const AtlasImage& item) {
        return item.name == image.name;
    });
    if (duplicate) return false;
    images_.push_back(std::move(image));
    return true;
}

bool TextureAtlasBuilder::build(TextureAtlas& output, std::string* error) const {
    if (images_.empty()) {
        setError(error, "Cannot build an empty texture atlas.");
        return false;
    }

    std::vector<std::size_t> order(images_.size());
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::stable_sort(order.begin(), order.end(), [this](std::size_t lhs, std::size_t rhs) {
        const AtlasImage& left = images_[lhs];
        const AtlasImage& right = images_[rhs];
        if (left.height != right.height) return left.height > right.height;
        return left.width > right.width;
    });

    std::vector<Placement> placements;
    placements.reserve(images_.size());
    int cursorX = padding_;
    int cursorY = padding_;
    int rowHeight = 0;
    int usedWidth = 1;
    int usedHeight = 1;
    for (const std::size_t imageIndex : order) {
        const AtlasImage& image = images_[imageIndex];
        if (image.width > maximumSize_ - padding_ * 2 ||
            image.height > maximumSize_ - padding_ * 2) {
            setError(error, "An atlas image exceeds the configured page size.");
            return false;
        }
        if (cursorX + image.width + padding_ > maximumSize_) {
            cursorX = padding_;
            cursorY += rowHeight + padding_;
            rowHeight = 0;
        }
        if (cursorY + image.height + padding_ > maximumSize_) {
            setError(error, "Atlas images do not fit in a single configured page.");
            return false;
        }
        placements.push_back({imageIndex, cursorX, cursorY});
        usedWidth = std::max(usedWidth, cursorX + image.width + padding_);
        usedHeight = std::max(usedHeight, cursorY + image.height + padding_);
        cursorX += image.width + padding_;
        rowHeight = std::max(rowHeight, image.height);
    }

    const std::size_t atlasWidth = static_cast<std::size_t>(usedWidth);
    const std::size_t atlasHeight = static_cast<std::size_t>(usedHeight);
    if (atlasWidth > std::numeric_limits<std::size_t>::max() / atlasHeight / 4U) {
        setError(error, "Atlas allocation size overflow.");
        return false;
    }
    std::vector<std::uint8_t> pixels(atlasWidth * atlasHeight * 4U, 0U);
    std::vector<AtlasRegion> regions;
    regions.reserve(images_.size());

    for (const Placement& placement : placements) {
        const AtlasImage& image = images_[placement.imageIndex];
        for (int y = -padding_; y < image.height + padding_; ++y) {
            const int sourceY = std::clamp(y, 0, image.height - 1);
            const int targetY = placement.y + y;
            if (targetY < 0 || targetY >= usedHeight) continue;
            for (int x = -padding_; x < image.width + padding_; ++x) {
                const int sourceX = std::clamp(x, 0, image.width - 1);
                const int targetX = placement.x + x;
                if (targetX < 0 || targetX >= usedWidth) continue;
                const std::size_t sourceIndex =
                    static_cast<std::size_t>((sourceY * image.width + sourceX) * 4);
                const std::size_t targetIndex =
                    (static_cast<std::size_t>(targetY) * atlasWidth +
                     static_cast<std::size_t>(targetX)) * 4U;
                std::copy_n(image.rgbaPixels.data() + sourceIndex, 4U,
                            pixels.data() + targetIndex);
            }
        }

        AtlasRegion region{};
        region.name = image.name;
        region.x = placement.x;
        region.y = placement.y;
        region.width = image.width;
        region.height = image.height;
        region.uv = {static_cast<float>(placement.x) / static_cast<float>(usedWidth),
                     static_cast<float>(placement.y + image.height) / static_cast<float>(usedHeight),
                     static_cast<float>(placement.x + image.width) / static_cast<float>(usedWidth),
                     static_cast<float>(placement.y) / static_cast<float>(usedHeight)};
        regions.push_back(std::move(region));
    }

    if (!output.upload(usedWidth, usedHeight, pixels.data(), std::move(regions))) {
        setError(error, "OpenGL could not upload the generated atlas page.");
        return false;
    }
    if (error != nullptr) error->clear();
    return true;
}

}  // namespace box::renderer
