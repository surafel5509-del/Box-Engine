// Path: engine/src/main/cpp/include/box/renderer/gltf_loader.hpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "box/renderer/mesh3d.hpp"

namespace box::renderer {

using GltfAssetReader =
    std::function<bool(const std::string& assetPath, std::vector<std::uint8_t>& output,
                       std::string* error)>;

/** Loads the documented mobile subset of glTF 2.0 JSON and GLB files into CPU-side data. */
class GltfLoader final {
public:
    static constexpr std::size_t kMaximumAssetBytes = 128U * 1024U * 1024U;
    static constexpr std::size_t kMaximumBufferBytes = 128U * 1024U * 1024U;
    static constexpr std::size_t kMaximumNodes = 4096U;
    static constexpr std::size_t kMaximumMeshes = 4096U;
    static constexpr std::size_t kMaximumPrimitives = 16384U;

    /**
     * Loads an asset using a caller-provided Android-asset or test reader. External URIs are
     * resolved relative to assetPath. output is replaced only after complete validation.
     */
    [[nodiscard]] static bool load(const std::string& assetPath,
                                   const GltfAssetReader& reader,
                                   GltfModel& output,
                                   std::string* error = nullptr);
};

}  // namespace box::renderer
