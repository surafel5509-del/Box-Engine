#pragma once

#include <android/asset_manager.h>

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "box/ecs/job_system.hpp"

namespace box::assets {

enum class AssetType : std::uint8_t { Texture, Audio, Font, Shader, Gltf, Binary };

struct AssetBlob {
    std::string path;
    AssetType type = AssetType::Binary;
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] std::string text() const {
        if (bytes.empty()) return {};
        return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
};

struct AssetLoadResult {
    std::shared_ptr<const AssetBlob> asset;
    std::string error;

    [[nodiscard]] bool succeeded() const noexcept { return asset != nullptr && error.empty(); }
};

/** Concurrent Android-asset reader with shared-future de-duplication and CPU-side caching. */
class AssetManager final {
public:
    explicit AssetManager(AAssetManager* androidAssets, std::size_t workerCount = 0U);
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    [[nodiscard]] std::shared_future<AssetLoadResult> request(const std::string& path,
                                                              AssetType type);
    void invalidate(const std::string& path, AssetType type);
    void clearCache();
    void waitForPendingLoads();
    void shutdown();
    [[nodiscard]] std::size_t cachedAssetCount() const;

private:
    [[nodiscard]] AssetLoadResult readAsset(const std::string& path, AssetType type) const;
    [[nodiscard]] static std::string cacheKey(const std::string& path, AssetType type);

    AAssetManager* androidAssets_ = nullptr;
    ecs::JobSystem workers_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_future<AssetLoadResult>> cache_;
};

}  // namespace box::assets
