#include "box/assets/asset_manager.hpp"

#include <android/asset_manager.h>

#include <exception>
#include <utility>

namespace box::assets {

AssetManager::AssetManager(AAssetManager* androidAssets, std::size_t workerCount)
    : androidAssets_(androidAssets), workers_(workerCount) {}

AssetManager::~AssetManager() { shutdown(); }

std::shared_future<AssetLoadResult> AssetManager::request(const std::string& path,
                                                          AssetType type) {
    const std::string key = cacheKey(path, type);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto existing = cache_.find(key);
    if (existing != cache_.end()) return existing->second;

    auto promise = std::make_shared<std::promise<AssetLoadResult>>();
    std::shared_future<AssetLoadResult> future = promise->get_future().share();
    cache_.emplace(key, future);
    const bool accepted = workers_.enqueue([this, path, type, promise]() {
        AssetLoadResult result{};
        try {
            result = readAsset(path, type);
        } catch (const std::exception& exception) {
            result.error = exception.what();
        } catch (...) {
            result.error = "Unknown exception while reading Android asset.";
        }
        promise->set_value(std::move(result));
    });
    if (!accepted) {
        AssetLoadResult result{};
        result.error = "Asset manager is shutting down.";
        promise->set_value(std::move(result));
    }
    return future;
}

void AssetManager::invalidate(const std::string& path, AssetType type) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.erase(cacheKey(path, type));
}

void AssetManager::clearCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

void AssetManager::waitForPendingLoads() { workers_.waitIdle(); }

void AssetManager::shutdown() { workers_.stop(); }

std::size_t AssetManager::cachedAssetCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.size();
}

AssetLoadResult AssetManager::readAsset(const std::string& path, AssetType type) const {
    AssetLoadResult result{};
    if (androidAssets_ == nullptr) {
        result.error = "Android AssetManager is not available.";
        return result;
    }
    if (path.empty()) {
        result.error = "Asset path must not be empty.";
        return result;
    }

    std::unique_ptr<AAsset, decltype(&AAsset_close)> asset(
        AAssetManager_open(androidAssets_, path.c_str(), AASSET_MODE_BUFFER), AAsset_close);
    if (asset == nullptr) {
        result.error = "Asset was not found: " + path;
        return result;
    }

    const off_t length = AAsset_getLength(asset.get());
    if (length < 0) {
        result.error = "Asset length is invalid: " + path;
        return result;
    }

    auto data = std::make_shared<AssetBlob>();
    data->path = path;
    data->type = type;
    data->bytes.resize(static_cast<std::size_t>(length));
    std::size_t totalRead = 0;
    while (totalRead < data->bytes.size()) {
        const int count = AAsset_read(asset.get(), data->bytes.data() + totalRead,
                                      data->bytes.size() - totalRead);
        if (count <= 0) break;
        totalRead += static_cast<std::size_t>(count);
    }
    asset.reset();
    if (totalRead != data->bytes.size()) {
        data->bytes.resize(totalRead);
        result.error = "Asset read was truncated: " + path;
        return result;
    }

    result.asset = std::move(data);
    return result;
}

std::string AssetManager::cacheKey(const std::string& path, AssetType type) {
    std::string key = path;
    key.push_back('\x1F');
    key.push_back(static_cast<char>(type));
    return key;
}

}  // namespace box::assets
